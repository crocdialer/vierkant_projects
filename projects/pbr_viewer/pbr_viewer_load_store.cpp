#include <fstream>

#include "pbr_viewer.hpp"
#include "ziparchive.h"
#include <crocore/filesystem.hpp>
#include <vierkant/cubemap_utils.hpp>

using double_second = std::chrono::duration<double>;
constexpr char g_zip_path[] = "asset_bundle_cache.zip";

void PBRViewer::load_model(const std::filesystem::path &path)
{
    vierkant::MeshPtr mesh;

    auto load_task = [this, path]() {
        m_num_loading++;
        auto start_time = std::chrono::steady_clock::now();
        auto mesh = load_mesh(path);

        auto done_cb = [this, mesh, /*lights = std::move(scene_assets.lights),*/ start_time, path]() {
            m_selected_objects.clear();

            // tmp test-loop
            for(uint32_t i = 0; i < 1; ++i)
            {
                auto object = vierkant::create_mesh_object(m_scene->registry(), {mesh});
                object->name = std::filesystem::path(path).filename().string();

                // scale
                object->transform.scale = glm::vec3(5.f / glm::length(object->aabb().half_extents()));

                // center aabb
                auto aabb = object->aabb().transform(vierkant::mat4_cast(object->transform));
                object->transform.translation = -aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 3.f * (float) i);

                m_scene->clear();
                m_scene->add_object(object);
            }
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

    save_scene();
}

PBRViewer::settings_t PBRViewer::load_settings(const std::filesystem::path &path)
{
    PBRViewer::settings_t settings = {};

    // initial pos
    settings.orbit_camera->spherical_coords = {1.1f, -0.5f};
    settings.orbit_camera->distance = 4.f;

    // create and open a character archive for input
    std::ifstream file_stream(path.string());

    // load data from archive
    if(file_stream.is_open())
    {
        try
        {
            cereal::JSONInputArchive archive(file_stream);

            // read class instance from archive
            archive(settings);
        } catch(std::exception &e)
        {
            spdlog::error(e.what());
        }

        spdlog::debug("loading settings: {}", path.string());
    }
    return settings;
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
            load_model(path);
            break;

        case crocore::filesystem::FileType::OTHER:
            if(std::filesystem::path(path).extension() == ".json")
            {
                auto loaded_scene = load_scene_data(path);
                if(loaded_scene)
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

void PBRViewer::save_asset_bundle(const vierkant::model::asset_bundle_t &asset_bundle,
                                  const std::filesystem::path &path)
{
    // save data to archive
    try
    {
        {
            spdlog::stopwatch sw;
            // create and open a character archive for output
            std::ofstream ofs(path.string(), std::ios_base::out | std::ios_base::binary);
            spdlog::debug("serializing/writing mesh_buffer_bundle: {}", path.string());
            cereal::BinaryOutputArchive archive(ofs);
            archive(asset_bundle);
            spdlog::debug("done serializing/writing mesh_buffer_bundle: {} ({})", path.string(), sw.elapsed());
        }

        spdlog::stopwatch sw;
        {
            std::unique_lock lock(m_bundle_rw_mutex);
            spdlog::debug("adding bundle to compressed archive: {} -> {}", path.string(), g_zip_path);
            vierkant::ziparchive zipstream(g_zip_path);
            zipstream.add_file(path);
        }
        spdlog::debug("done compressing bundle: {} -> {} ({})", path.string(), g_zip_path, sw.elapsed());
        std::filesystem::remove(path);
    } catch(std::exception &e)
    {
        spdlog::error(e.what());
    }
}

std::optional<vierkant::model::asset_bundle_t> PBRViewer::load_asset_bundle(const std::filesystem::path &path)
{
    vierkant::ziparchive zip(g_zip_path);

    if(zip.has_file(path))
    {
        try
        {
//            spdlog::trace("archive '{}': {}", g_zip_path, zip.contents());
            spdlog::debug("loading bundle '{}' from archive '{}'", path.string(), g_zip_path);
            vierkant::model::asset_bundle_t ret;

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

void PBRViewer::save_scene(const std::filesystem::path &path) const
{
    // scene traversal
    scene_data_t data;
    data.environment_path = m_scene_data.environment_path;

    auto cam_view = m_scene->registry()->view<vierkant::Object3D *, vierkant::physical_camera_params_t>();

    for(const auto &[entity, object, cam_params]: cam_view.each())
    {
        scene_camera_t scene_camera = {};
        scene_camera.name = object->name;
        scene_camera.transform = object->transform;
        scene_camera.params = cam_params;
        data.cameras.push_back(scene_camera);
    }

    // set of meshes -> indices / paths !?
    std::map<vierkant::MeshConstPtr, size_t> mesh_indices;
    std::map<std::filesystem::path, size_t> path_map;

    auto view = m_scene->registry()->view<vierkant::Object3D *, vierkant::mesh_component_t>();

    for(const auto &[entity, object, mesh_component]: view.each())
    {
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

    for(const auto &[entity, object, mesh_component]: view.each())
    {
        scene_node_t node = {};

        // transforms / mesh/index
        node.name = object->name;
        node.mesh_index = mesh_indices[mesh_component.mesh];
        node.entry_indices = mesh_component.entry_indices;
        node.transform = object->global_transform();
        if(object->has_component<vierkant::animation_state_t>())
        {
            node.animation_state = object->get_component<vierkant::animation_state_t>();
        }
        data.nodes.push_back(node);
    }

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
        std::vector<scene_camera_t> cameras;

        if(scene_data)
        {
            for(const auto &p: scene_data->model_paths) { meshes.push_back(load_mesh(p)); }
            nodes = scene_data->nodes;
            cameras = scene_data->cameras;
        }
        else
        {
            meshes.push_back(load_mesh(""));
            scene_node_t n = {"hasslecube", 0};
            nodes.push_back(n);
        }

        auto done_cb = [this, nodes = std::move(nodes), meshes = std::move(meshes), cameras = std::move(cameras)
                        /*lights = std::move(scene_assets.lights),*/]() {
            if(!nodes.empty())
            {
                m_scene->clear();
                for(const auto &node: nodes)
                {
                    assert(node.mesh_index < meshes.size());

                    vierkant::mesh_component_t mesh_component = {meshes[node.mesh_index], node.entry_indices};
                    auto object = vierkant::create_mesh_object(m_scene->registry(),
                                                               {meshes[node.mesh_index], node.entry_indices});
                    object->name = node.name;
                    object->transform = node.transform;
                    if(node.animation_state && object->has_component<vierkant::animation_state_t>())
                    {
                        object->get_component<vierkant::animation_state_t>() = *node.animation_state;
                    }
                    m_scene->add_object(object);
                }

                for(const auto &cam: cameras)
                {
                    auto object = vierkant::PerspectiveCamera::create(m_scene->registry(), cam.params);
                    object->name = cam.name;
                    object->transform = cam.transform;
                    m_scene->add_object(object);

                    m_camera = object;
                }
                if(m_path_tracer) { m_path_tracer->reset_accumulator(); }
            }
        };
        main_queue().post(done_cb);
    };
    background_queue().post(load_task);
}

vierkant::MeshPtr PBRViewer::load_mesh(const std::filesystem::path &path)
{
    // additionally required buffer-flags for raytracing/compute/mesh-shading
    VkBufferUsageFlags buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if(m_settings.enable_raytracing_pipeline_features)
    {
        buffer_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }
    m_num_loading++;
    auto start_time = std::chrono::steady_clock::now();
    vierkant::MeshPtr mesh;

    if(!path.empty())
    {
        spdlog::debug("loading model '{}'", path.string());


        // create hash of filename+params, search existing bundle
        std::optional<vierkant::model::asset_bundle_t> bundle;
        size_t hash_val = std::hash<std::string>()(path.filename().string());
        vierkant::hash_combine(hash_val, m_settings.mesh_buffer_params);
        std::filesystem::path bundle_path =
                fmt::format("{}_{}.bin", std::filesystem::path(path).filename().string(), hash_val);

        bool bundle_created = false;
        bundle = load_asset_bundle(bundle_path);

        if(!bundle)
        {
            // tinygltf
            auto scene_assets = vierkant::model::load_model(path, &background_queue());
            spdlog::debug("loaded model '{}' ({})", path.string(),
                          vierkant::double_second(std::chrono::steady_clock::now() - start_time));

            if(!scene_assets || scene_assets->entry_create_infos.empty())
            {
                spdlog::warn("could not load file: {}", path.string());
                return {};
            }

            spdlog::stopwatch sw;

            spdlog::debug("creating asset-bundle '{}' - lod: {} - meshlets: {} - bc7-compression: {}",
                          bundle_path.string(), m_settings.mesh_buffer_params.generate_lods,
                          m_settings.mesh_buffer_params.generate_meshlets, m_settings.texture_compression);

            vierkant::model::asset_bundle_t asset_bundle;
            asset_bundle.mesh_buffer_bundle =
                    vierkant::create_mesh_buffers(scene_assets->entry_create_infos, m_settings.mesh_buffer_params);

            // run in-place compression on all textures, store compressed textures in bundle
            if(m_settings.texture_compression && scene_assets)
            {
                vierkant::model::compress_textures(*scene_assets);
                asset_bundle.textures = scene_assets->textures;
                asset_bundle.materials = scene_assets->materials;
                asset_bundle.texture_samplers = scene_assets->texture_samplers;
            }

            bundle = std::move(asset_bundle);
            spdlog::debug("asset-bundle '{}' done -> {}", bundle_path.string(), sw.elapsed());
            bundle_created = true;
        }

        vierkant::model::load_mesh_params_t load_params = {};
        load_params.device = m_device;
        load_params.load_queue = m_queue_model_loading;
        load_params.mesh_buffers_params = m_settings.mesh_buffer_params;
        load_params.buffer_flags = buffer_flags;
        mesh = vierkant::model::load_mesh(load_params, {}, bundle);

        m_num_loading--;

        if(bundle_created && m_settings.cache_mesh_bundles)
        {
            background_queue().post(
                    [this, bundle = std::move(bundle), bundle_path]() { save_asset_bundle(*bundle, bundle_path); });
        }
    }
    else
    {
        auto geom = vierkant::Geometry::Box(glm::vec3(.5f));
        geom->colors.clear();

        vierkant::Mesh::create_info_t mesh_create_info = {};
        mesh_create_info.mesh_buffer_params = m_settings.mesh_buffer_params;
        mesh_create_info.buffer_usage_flags = buffer_flags;
        mesh = vierkant::Mesh::create_from_geometry(m_device, geom, mesh_create_info);
        auto mat = vierkant::Material::create();

        auto it = m_textures.find("test");
        if(it != m_textures.end()) { mat->textures[vierkant::Material::Color] = it->second; }
        mesh->materials = {mat};
    }

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
