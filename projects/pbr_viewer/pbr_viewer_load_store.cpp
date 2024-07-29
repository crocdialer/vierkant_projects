#include <fstream>

#include "pbr_viewer.hpp"
#include "ziparchive.h"
#include <crocore/filesystem.hpp>
#include <vierkant/Visitor.hpp>
#include <vierkant/cubemap_utils.hpp>

using double_second = std::chrono::duration<double>;
constexpr char g_cache_path[] = "cache";
constexpr char g_zip_path[] = "cache.zip";

void PBRViewer::load_model(const std::filesystem::path &path, bool clear_scene)
{
    vierkant::MeshPtr mesh;

    auto load_task = [this, path, clear_scene]() {
        m_num_loading++;
        auto start_time = std::chrono::steady_clock::now();
        auto mesh = load_mesh(path);

        auto done_cb = [this, mesh, start_time, path, clear_scene]() {
            m_selected_objects.clear();

            auto object = m_scene->create_mesh_object({mesh});
            object->name = std::filesystem::path(path).filename().string();

            // scale
            object->transform.scale = glm::vec3(5.f / glm::length(object->aabb().half_extents()));

            // center aabb
            auto aabb = object->aabb().transform(object->transform);
            object->transform.translation = -aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 3.f);

            if(clear_scene) { m_scene->clear(); }
            m_scene->add_object(object);
            if(m_path_tracer) { m_path_tracer->reset_accumulator(); }

            auto dur = double_second(std::chrono::steady_clock::now() - start_time);
            spdlog::debug("loaded '{}' -- ({:03.2f})", path.string(), dur.count());
            m_num_loading--;
        };
        if(mesh) { main_queue().post(done_cb); }
    };
    background_queue().post(load_task);
}

void PBRViewer::load_environment(const std::string &path)
{
    auto load_task = [&, path]() {
        m_num_loading++;

        auto start_time = std::chrono::steady_clock::now();

        vierkant::ImagePtr panorama, skybox, conv_lambert, conv_ggx;
        auto img = crocore::create_image_from_file(path, 4);

        if(img)
        {
            constexpr VkFormat hdr_format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;

            bool use_float = (img->num_bytes() / (img->width() * img->height() * img->num_components())) > 1;

            // command pool for background transfer
            auto command_pool = vierkant::create_command_pool(m_device, vierkant::Device::Queue::GRAPHICS,
                                                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

            {
                auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());
                cmd_buf.begin();

                vierkant::Image::Format fmt = {};
                fmt.extent = {img->width(), img->height(), 1};
                fmt.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                fmt.format = use_float ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
                fmt.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                fmt.initial_cmd_buffer = cmd_buf.handle();
                panorama = vierkant::Image::create(m_device, nullptr, fmt);

                auto buf = vierkant::Buffer::create(m_device, img->data(), img->num_bytes(),
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

                // copy and layout transition
                panorama->copy_from(buf, cmd_buf.handle());
                panorama->transition_layout(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(m_queue_image_loading, true);

                // derive sane resolution for cube from panorama-width
                uint32_t res = crocore::next_pow_2(std::max(img->width(), img->height()) / 4);
                skybox = vierkant::cubemap_from_panorama(m_device, panorama, m_queue_image_loading, res, true,
                                                         hdr_format);
            }

            if(skybox)
            {
                constexpr uint32_t lambert_size = 128;
                conv_lambert = vierkant::create_convolution_lambert(m_device, skybox, lambert_size, hdr_format,
                                                                    m_queue_image_loading);
                conv_ggx = vierkant::create_convolution_ggx(m_device, skybox, skybox->width(), hdr_format,
                                                            m_queue_image_loading);

                auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());
                cmd_buf.begin();

                conv_lambert->transition_layout(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, cmd_buf.handle());
                conv_ggx->transition_layout(VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(m_queue_image_loading, true);
            }
        }

        main_queue().post([this, path, skybox, conv_lambert, conv_ggx, start_time]() {
            m_scene->set_environment(skybox);

            m_pbr_renderer->set_environment(conv_lambert, conv_ggx);

            if(m_path_tracer) { m_path_tracer->reset_accumulator(); }

            m_scene_data.environment_path = path;
            auto dur = double_second(std::chrono::steady_clock::now() - start_time);
            spdlog::debug("loaded '{}' -- ({:03.2f})", path, dur.count());
            m_num_loading--;
        });
    };
    background_queue().post(load_task);
}

void PBRViewer::save_settings(PBRViewer::settings_t settings, const std::filesystem::path &path) const
{
    // window settings
    vierkant::Window::create_info_t window_info = {};
    window_info.size = m_window->size();
    window_info.position = m_window->position();
    window_info.fullscreen = m_window->fullscreen();
    window_info.sample_count = m_window->swapchain().sample_count();
    window_info.title = m_window->title();
    window_info.vsync = m_window->swapchain().v_sync();
    settings.window_info = window_info;

    // logger settings
    settings.log_level = spdlog::get_level();

    // target-fps
    settings.target_fps = static_cast<float>(target_loop_frequency);

    // camera-control settings
    settings.use_fly_camera = m_camera_control.current == m_camera_control.fly;
    settings.orbit_camera = m_camera_control.orbit;
    settings.fly_camera = m_camera_control.fly;

    // renderer settings
    settings.pbr_settings = m_pbr_renderer->settings;
    if(m_path_tracer) { settings.path_tracer_settings = m_path_tracer->settings; }
    settings.path_tracing = m_scene_renderer == m_path_tracer;

    // create and open a character archive for output
    std::ofstream ofs(path.string());

    // save data to archive
    try
    {
        cereal::JSONOutputArchive archive(ofs);

        // write class instance to archive
        archive(settings);

    } catch(std::exception &e)
    {
        spdlog::error(e.what());
    }

    spdlog::debug("save settings: {}", path.string());
}

std::optional<PBRViewer::settings_t> PBRViewer::load_settings(const std::filesystem::path &path)
{
    // create and open a character archive for input
    std::ifstream file_stream(path.string());

    // load data from archive
    if(file_stream.is_open())
    {
        try
        {
            cereal::JSONInputArchive archive(file_stream);

            // read class instance from archive
            PBRViewer::settings_t settings = {};
            archive(settings);
            return settings;
        } catch(std::exception &e)
        {
            spdlog::error(e.what());
        }

        spdlog::debug("loading settings: {}", path.string());
    }
    return {};
}

void PBRViewer::load_file(const std::string &path)
{
    auto add_to_recent_files = [this](const std::string &f) {
        main_queue().post([this, f] {
            m_settings.recent_files.push_back(f);
            while(m_settings.recent_files.size() > 10) { m_settings.recent_files.pop_front(); }
        });
    };

    switch(crocore::filesystem::get_file_type(path))
    {
        case crocore::filesystem::FileType::IMAGE:
            add_to_recent_files(path);
            load_environment(path);
            break;

        case crocore::filesystem::FileType::MODEL:
            add_to_recent_files(path);
            load_model(path, false);
            break;

        case crocore::filesystem::FileType::OTHER:
            if(std::filesystem::path(path).extension() == ".json")
            {
                if(auto loaded_scene = load_scene_data(path))
                {
                    m_scene->clear();
                    add_to_recent_files(path);
                    build_scene(loaded_scene);
                }
            }
            break;
        default: break;
    }
}

void PBRViewer::save_scene(const std::filesystem::path &path) const
{
    // scene traversal
    scene_data_t data;
    data.environment_path = m_scene_data.environment_path;

    vierkant::SelectVisitor<vierkant::Camera> camera_filter;
    m_scene->root()->accept(camera_filter);

    for(const auto &cam: camera_filter.objects)
    {
        scene_camera_t scene_camera = {};
        scene_camera.name = cam->name;
        scene_camera.transform = cam->transform;
        scene_camera.params = cam->params();
        data.cameras.push_back(scene_camera);
    }

    // set of meshes -> indices / paths !?
    std::map<vierkant::MeshConstPtr, size_t> mesh_indices;
    std::map<vierkant::Object3D *, size_t> obj_to_node_index;
    std::map<std::filesystem::path, size_t> path_map;

    vierkant::LambdaVisitor visitor;
    visitor.traverse(*m_scene->root(), [&](vierkant::Object3D &obj) -> bool {
        if(&obj == m_scene->root().get()) { return true; }

        // skip cameras
        if(dynamic_cast<vierkant::Camera *>(&obj)) { return true; }

        scene_node_t node = {};
        node.name = obj.name;
        node.transform = obj.transform;

        if(obj.has_component<vierkant::animation_component_t>())
        {
            node.animation_state = obj.get_component<vierkant::animation_component_t>();
        }

        if(obj.has_component<vierkant::physics_component_t>())
        {
            node.physics_state = obj.get_component<vierkant::physics_component_t>();
        }

        obj_to_node_index[&obj] = data.nodes.size();
        data.nodes.push_back(node);

        if(obj.has_component<vierkant::mesh_component_t>())
        {
            auto mesh_component = obj.get_component<vierkant::mesh_component_t>();
            const auto &mesh = mesh_component.mesh;

            if(!mesh_indices.contains(mesh))
            {
                auto path_it = m_model_paths.find(mesh);
                if(path_it != m_model_paths.end())
                {
                    size_t index;

                    if(!path_map.contains(path_it->second))
                    {
                        path_map[path_it->second] = data.model_paths.size();
                        index = data.model_paths.size();
                        data.model_paths.push_back(path_it->second.string());
                    }
                    else { index = path_map.at(path_it->second); }
                    mesh_indices[mesh] = index;
                }
            }
        }
        return true;
    });

    // add top-lvl scenegraph-nodes
    for(const auto &child: m_scene->root()->children) { data.scene_roots.push_back(obj_to_node_index[child.get()]); }

    visitor.traverse(*m_scene->root(), [&](vierkant::Object3D &obj) -> bool {
        if(!obj_to_node_index.contains(&obj)) { return true; }
        auto &node = data.nodes[obj_to_node_index[&obj]];
        for(const auto &child: obj.children) { node.children.push_back(obj_to_node_index[child.get()]); }

        if(obj.has_component<vierkant::mesh_component_t>())
        {
            auto mesh_component = obj.get_component<vierkant::mesh_component_t>();
            node.mesh_index = mesh_indices[mesh_component.mesh];
            node.entry_indices = mesh_component.entry_indices;

            // store materials with dirty hashes
            for(const auto &mat: mesh_component.mesh->materials)
            {
                if(mat->hash != std::hash<vierkant::material_t>()(mat->m)) { data.materials[mat->m.id] = mat->m; }
            }
        }
        return true;
    });

    // save scene_data
    std::ofstream ofs(path.string());

    // save data to archive
    try
    {
        cereal::JSONOutputArchive archive(ofs);
        archive(data);

    } catch(std::exception &e)
    {
        spdlog::error(e.what());
    }
}

void PBRViewer::build_scene(const std::optional<scene_data_t> &scene_data)
{
    auto load_task = [this, scene_data]() {
        // load background
        if(scene_data) { load_file(scene_data->environment_path); }

        // TODO: use threadpool
        std::vector<vierkant::MeshPtr> meshes;
        std::vector<scene_node_t> nodes;
        std::vector<uint32_t> scene_roots;
        std::vector<scene_camera_t> cameras;

        if(scene_data)
        {
            for(const auto &p: scene_data->model_paths)
            {
                auto mesh = load_mesh(p);

                if(mesh)
                {
                    // optional material override(s)
                    for(auto &mat: mesh->materials)
                    {
                        auto it = scene_data->materials.find(mat->m.id);
                        if(it != scene_data->materials.end())
                        {
                            mat->m = it->second;
                            spdlog::trace("overriding material: {}", mat->m.name);
                        }
                    }
                    meshes.push_back(mesh);
                }
            }
            nodes = scene_data->nodes;
            cameras = scene_data->cameras;
            scene_roots = scene_data->scene_roots;
        }
        else
        {
            meshes.push_back(load_mesh(""));
            scene_node_t node = {};
            node.name = "hasslecube";
            node.mesh_index = 0;
            nodes = {node};
            scene_roots = {0};
        }

        auto done_cb = [this, nodes = std::move(nodes), meshes = std::move(meshes), cameras = std::move(cameras),
                        scene_roots = std::move(scene_roots)]() {
            if(!nodes.empty())
            {
                m_scene->clear();

                std::vector<vierkant::Object3DPtr> objects;

                // create objects for all nodes
                for(const auto &node: nodes)
                {
                    vierkant::Object3DPtr obj;

                    if(node.mesh_index && *node.mesh_index < meshes.size())
                    {
                        const auto &mesh = meshes[*node.mesh_index];
                        obj = m_scene->create_mesh_object({mesh, node.entry_indices});
                    }
                    else
                    {
                        obj = vierkant::Object3D::create(m_scene->registry());
                    }
                    obj->name = node.name;
                    obj->transform = node.transform;
                    if(node.animation_state) { obj->add_component(*node.animation_state); }
                    if(node.physics_state) { obj->add_component(*node.physics_state); }

                    objects.push_back(obj);
                }

                // recreate node-hierarchy
                for(uint32_t i = 0; i < nodes.size(); ++i)
                {
                    for(auto child_index: nodes[i].children) { objects[i]->add_child(objects[child_index]); }
                }

                // add scene-roots
                for(auto idx: scene_roots) { m_scene->add_object(objects[idx]); }

                for(const auto &cam: cameras)
                {
                    auto object = std::visit(
                            [this](auto &&camera_params) -> vierkant::CameraPtr {
                                using T = std::decay_t<decltype(camera_params)>;

                                if constexpr(std::is_same_v<T, vierkant::ortho_camera_params_t>)
                                {
                                    return vierkant::OrthoCamera::create(m_scene->registry(), camera_params);
                                }
                                else if constexpr(std::is_same_v<T, vierkant::physical_camera_params_t>)
                                {
                                    return vierkant::PerspectiveCamera::create(m_scene->registry(), camera_params);
                                }
                                else
                                    return nullptr;
                            },
                            cam.params);

                    object->name = cam.name;
                    object->transform = cam.transform;
                    m_scene->add_object(object);

                    m_camera = object;
                }
                if(m_path_tracer) { m_path_tracer->reset_accumulator(); }
            }

            vierkant::Object3DPtr ground;
            auto results = m_scene->objects_by_name("ground");
            if(results.empty())
            {
                ground = vierkant::Object3D::create(m_scene->registry());
                ground->name = "ground";
                ground->transform.translation.y = -.2f;
                auto &cmp = ground->add_component<vierkant::physics_component_t>();
                cmp.shape = vierkant::collision::box_t{.half_extents = {20.f, .2f, 20.f}};
            }
            else { ground = results.front()->shared_from_this(); }
            m_scene->remove_object(ground);
            m_scene->add_object(ground);

            vierkant::PhysicsContext::callbacks_t callbacks;
            callbacks.contact_begin = [this](uint32_t /*obj1*/, uint32_t obj2) {
                if(auto obj = m_scene->object_by_id(obj2))
                {
                    spdlog::debug("{} hit the ground (thread: {:02X})", obj->name,
                                  std::hash<std::thread::id>()(std::this_thread::get_id()));
                }
            };
            callbacks.contact_end = [this](uint32_t /*obj1*/, uint32_t obj2) {
                if(auto obj = m_scene->object_by_id(obj2)) { spdlog::debug("{} bounced", obj->name); }
            };
            m_scene->context().set_callbacks(ground->id(), callbacks);
        };
        main_queue().post(done_cb);
    };
    background_queue().post(load_task);
}

vierkant::MeshPtr PBRViewer::load_mesh(const std::filesystem::path &path)
{
    m_num_loading++;
    auto start_time = std::chrono::steady_clock::now();
    vierkant::MeshPtr mesh;

    if(!path.empty())
    {
        spdlog::debug("loading model '{}'", path.string());

        // create hash of filename+params, search existing bundle
        size_t hash_val = std::hash<std::string>()(path.filename().string());
        vierkant::hash_combine(hash_val, m_settings.mesh_buffer_params);
        vierkant::hash_combine(hash_val, m_settings.texture_compression);
        std::filesystem::path bundle_path =
                std::filesystem::path(g_cache_path) /
                fmt::format("{}_{}.bin", std::filesystem::path(path).filename().string(), hash_val);

        auto mesh_id = vierkant::MeshId::from_name(bundle_path.string());
        bool bundle_created = false;
        auto model_assets = load_asset_bundle(bundle_path);

        if(!model_assets)
        {
            // tinygltf
            model_assets = vierkant::model::load_model(path, &background_queue());
            spdlog::debug("loaded model '{}' ({})", path.string(),
                          double_second(std::chrono::steady_clock::now() - start_time));

            if(!model_assets)
            {
                spdlog::warn("could not load file: {}", path.string());
                return {};
            }

            spdlog::stopwatch sw;

            spdlog::debug("creating asset-bundle '{}' - lod: {} - meshlets: {} - bc7-compression: {}",
                          bundle_path.string(), m_settings.mesh_buffer_params.generate_lods,
                          m_settings.mesh_buffer_params.generate_meshlets, m_settings.texture_compression);

            // run compression of geometries, creation of meshlets, lods, etc.
            model_assets->geometry_data = vierkant::create_mesh_buffers(
                    std::get<std::vector<vierkant::Mesh::entry_create_info_t>>(model_assets->geometry_data),
                    m_settings.mesh_buffer_params);

            // run in-place compression on all textures, store compressed textures in bundle
            if(m_settings.texture_compression)
            {
                vierkant::model::compress_textures(*model_assets, &background_queue());
            }

            spdlog::debug("asset-bundle '{}' done -> {}", bundle_path.string(), sw.elapsed());
            bundle_created = true;
        }

        vierkant::model::load_mesh_params_t load_params = {};
        load_params.device = m_device;
        load_params.load_queue = m_queue_model_loading;
        load_params.mesh_buffers_params = m_settings.mesh_buffer_params;
        load_params.buffer_flags = m_mesh_buffer_flags;
        mesh = vierkant::model::load_mesh(load_params, *model_assets);

        m_num_loading--;

        // store in application mesh-lut
        m_mesh_map[mesh_id] = {.mesh = mesh,
                               .bundle = std::get<vierkant::mesh_buffer_bundle_t>(model_assets->geometry_data)};

        if(bundle_created && m_settings.cache_mesh_bundles)
        {
            background_queue().post([this, mesh_assets = std::move(model_assets), bundle_path]() {
                save_asset_bundle(*mesh_assets, bundle_path);
            });
        }
    }
    else { mesh = m_box_mesh; }

    // store mesh/path
    m_model_paths[mesh] = path;

    return mesh;
}

std::optional<PBRViewer::scene_data_t> PBRViewer::load_scene_data(const std::filesystem::path &path)
{
    // create and open a character archive for input
    std::ifstream file_stream(path.string());

    // load data from archive
    if(file_stream.is_open())
    {
        try
        {
            cereal::JSONInputArchive archive(file_stream);
            PBRViewer::scene_data_t scene_data;
            spdlog::debug("loading scene: {}", path.string());
            archive(scene_data);
            return scene_data;
        } catch(std::exception &e)
        {
            spdlog::warn(e.what());
        }
    }
    return {};
}

void PBRViewer::save_asset_bundle(const vierkant::model::model_assets_t &mesh_assets, const std::filesystem::path &path)
{
    // save data to archive
    try
    {
        {
            spdlog::stopwatch sw;

            // create directory, if necessary
            std::filesystem::create_directories(crocore::filesystem::get_directory_part(path));

            // create and open a character archive for output
            std::ofstream ofs(path.string(), std::ios_base::out | std::ios_base::binary);
            spdlog::debug("serializing/writing mesh_buffer_bundle: {}", path.string());
            cereal::BinaryOutputArchive archive(ofs);
            archive(mesh_assets);
            spdlog::debug("done serializing/writing mesh_buffer_bundle: {} ({})", path.string(), sw.elapsed());
        }

        if(m_settings.cache_zip_archive)
        {
            spdlog::stopwatch sw;
            {
                std::unique_lock lock(m_bundle_rw_mutex);
                spdlog::debug("adding bundle to compressed archive: {} -> {}", path.string(), g_zip_path);
                vierkant::ziparchive zipstream(g_zip_path);
                zipstream.add_file(path);
            }
            spdlog::debug("done compressing bundle: {} -> {} ({})", path.string(), g_zip_path, sw.elapsed());
            std::filesystem::remove(path);
        }
    } catch(std::exception &e)
    {
        spdlog::error(e.what());
    }
}

std::optional<vierkant::model::model_assets_t> PBRViewer::load_asset_bundle(const std::filesystem::path &path)
{
    {
        std::ifstream cache_file(path);

        if(cache_file.is_open())
        {
            try
            {
                spdlog::debug("loading bundle '{}'", path.string());
                vierkant::model::model_assets_t ret;

                std::shared_lock lock(m_bundle_rw_mutex);
                cereal::BinaryInputArchive archive(cache_file);
                archive(ret);
                return ret;
            } catch(std::exception &e)
            {
                spdlog::error(e.what());
            }
        }
    }

    vierkant::ziparchive zip(g_zip_path);

    if(zip.has_file(path))
    {
        try
        {
            //            spdlog::trace("archive '{}': {}", g_zip_path, zip.contents());
            spdlog::debug("loading bundle '{}' from archive '{}'", path.string(), g_zip_path);
            vierkant::model::model_assets_t ret;

            std::shared_lock lock(m_bundle_rw_mutex);
            auto zipstream = zip.open_file(path);
            cereal::BinaryInputArchive archive(zipstream);
            archive(ret);
            return ret;
        } catch(std::exception &e)
        {
            spdlog::error(e.what());
        }
    }
    return {};
}