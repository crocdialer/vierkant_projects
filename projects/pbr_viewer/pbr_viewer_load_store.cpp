#include "pbr_viewer.hpp"

#include <crocore/filesystem.hpp>
#include <cxxopts.hpp>
#include <fstream>
#include <vierkant/Visitor.hpp>
#include <vierkant/cubemap_utils.hpp>

using double_second = std::chrono::duration<double>;

//! define a custom object-component, used to help with (sub)scene serialization
struct object_flags_component_t
{
    VIERKANT_ENABLE_AS_COMPONENT();
    SceneId scene_id = SceneId::nil();
};

void PBRViewer::add_to_recent_files(const std::filesystem::path &f)
{
    main_queue().post([this, f] {
        m_settings.recent_files.push_back(f.string());
        while(m_settings.recent_files.size() > 10) { m_settings.recent_files.pop_front(); }
    });
};

void PBRViewer::load_model(const load_model_params_t &params)
{
    vierkant::MeshPtr mesh;

    auto load_task = [this, params]() {
        m_num_loading++;
        auto start_time = std::chrono::steady_clock::now();
        auto mesh = load_mesh(params.path);

        auto done_cb = [this, mesh, start_time, params]() {
            m_selected_objects.clear();

            vierkant::Object3DPtr object;

            if(params.load_as_mesh_library)
            {
                object = m_object_store->create_object();

                // iterate mesh-entries, create sub-objects
                vierkant::mesh_component_t mesh_component = {mesh};

                // set library flag
                mesh_component.library = true;

                for(uint32_t i = 0; i < mesh->entries.size(); ++i)
                {
                    auto &mesh_entry = mesh->entries[i];
                    mesh_component.entry_indices = {i};
                    auto entry_obj = m_scene->create_mesh_object(mesh_component);

                    // inherit name and transform from entry
                    entry_obj->name = mesh_entry.name;
                    entry_obj->transform = mesh_entry.transform;

                    // add as child-object
                    object->add_child(entry_obj);
                }
            }
            else { object = m_scene->create_mesh_object({mesh}); }

            object->name = std::filesystem::path(params.path).filename().string();

            if(params.normalize_size)
            {
                // scale
                object->transform.scale = glm::vec3(5.f / glm::length(object->aabb().half_extents()));

                // center aabb
                auto aabb = object->aabb().transform(object->transform);
                object->transform.translation = -aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 3.f);
            }

            if(params.clear_scene) { m_scene->clear(); }
            m_scene->add_object(object);
            if(m_path_tracer) { m_path_tracer->reset_accumulator(); }

            auto dur = double_second(std::chrono::steady_clock::now() - start_time);
            spdlog::debug("loaded '{}' -- ({:03.2f})", params.path.string(), dur.count());
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
            // acquire lock for image-queue // TODO: a bit more fine-grained!?
            auto lock = std::lock_guard(*m_device->queue_asset(m_queue_image_loading)->mutex);

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
                {
                    cmd_buf.submit(m_queue_image_loading);

                    // derive sane resolution for cube from panorama-width
                    uint32_t res = crocore::next_pow_2(std::max(img->width(), img->height()) / 4);
                    skybox = vierkant::cubemap_from_panorama(m_device, panorama, m_queue_image_loading, res, true,
                                                             m_hdr_format);
                }
            }

            if(skybox)
            {
                constexpr uint32_t lambert_size = 128;
                conv_lambert = vierkant::create_convolution_lambert(m_device, skybox, lambert_size, m_hdr_format,
                                                                    m_queue_image_loading);
                conv_ggx = vierkant::create_convolution_ggx(m_device, skybox, skybox->width(), m_hdr_format,
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

void PBRViewer::load_file(const std::string &path, bool clear)
{
    switch(crocore::filesystem::get_file_type(path))
    {
        case crocore::filesystem::FileType::IMAGE:
            add_to_recent_files(path);
            load_environment(path);
            break;

        case crocore::filesystem::FileType::MODEL:
        {
            add_to_recent_files(path);
            load_model_params_t load_params = {path};
            load_params.clear_scene = clear;
            load_model(load_params);
            break;
        }

        case crocore::filesystem::FileType::OTHER:
            if(std::filesystem::path(path).extension() == ".json")
            {
                if(auto loaded_scene = load_scene_data(path))
                {
                    if(clear) { m_scene->clear(); }
                    add_to_recent_files(path);
                    SceneId scene_id;
                    m_scene_paths[scene_id] = path;
                    build_scene(loaded_scene, clear, scene_id);
                }
            }
            break;
        default: break;
    }
}

void PBRViewer::save_scene(std::filesystem::path path)
{
    // handle empty path
    if(path.empty())
    {
        auto it = m_scene_paths.find(m_scene_id);
        if(it != m_scene_paths.end()) { path = it->second; }
        else
        {
            spdlog::warn("{}: unable to figure out save-path", __func__);
            return;
        }
    }
    spdlog::debug("save scene: {}", path.string());
    m_scene_paths[m_scene_id] = path;

    // scene traversal
    scene_data_t data;
    data.name = m_scene->root()->name;
    data.environment_path = m_scene_data.environment_path;

    // set of mesh-ids
    std::unordered_set<vierkant::MeshId> mesh_ids;
    std::map<vierkant::Object3D *, size_t> obj_to_node_index;

    vierkant::LambdaVisitor visitor;
    visitor.traverse(*m_scene->root(), [&](vierkant::Object3D &obj) -> bool {
        if(&obj == m_scene->root().get()) { return true; }

        // skip cameras
        if(auto *cam = dynamic_cast<vierkant::Camera *>(&obj))
        {
            scene_camera_t &scene_camera = data.cameras.emplace_back();
            scene_camera.name = cam->name;
            scene_camera.transform = cam->transform;
            scene_camera.params = cam->params();
            return true;
        }

        obj_to_node_index[&obj] = data.nodes.size();

        scene_node_t &node = data.nodes.emplace_back();
        node.name = obj.name;
        node.enabled = obj.enabled;
        node.transform = obj.transform;

        if(auto *flags_cmp = obj.get_component_ptr<object_flags_component_t>())
        {
            if(flags_cmp->scene_id)
            {
                node.scene_id = flags_cmp->scene_id;

                auto path_it = m_scene_paths.find(flags_cmp->scene_id);
                if(path_it != m_scene_paths.end()) { data.scene_paths[flags_cmp->scene_id] = path_it->second.string(); }

                // handled as subscene, bail out
                return false;
            }
        }

        if(obj.has_component<vierkant::animation_component_t>())
        {
            node.animation_state = obj.get_component<vierkant::animation_component_t>();
        }

        if(obj.has_component<vierkant::physics_component_t>())
        {
            node.physics_state = obj.get_component<vierkant::physics_component_t>();
        }

        if(obj.has_component<vierkant::mesh_component_t>())
        {
            auto mesh_component = obj.get_component<vierkant::mesh_component_t>();
            const auto &mesh = mesh_component.mesh;

            if(!mesh_ids.contains(mesh->id))
            {
                auto path_it = m_model_paths.find(mesh->id);
                if(path_it != m_model_paths.end())
                {
                    data.model_paths[mesh->id] = path_it->second.string();
                    mesh_ids.insert(mesh->id);
                }
            }
        }
        return true;
    });

    // add top-lvl scenegraph-nodes
    for(const auto &child: m_scene->root()->children) { data.scene_roots.push_back(obj_to_node_index[child.get()]); }

    visitor.traverse(*m_scene->root(), [&](vierkant::Object3D &obj) -> bool {
        if(!obj_to_node_index.contains(&obj)) { return true; }

        // skip object from sub-scenes
        if(auto *flags_cmp = obj.get_component_ptr<object_flags_component_t>())
        {
            if(flags_cmp->scene_id) { return false; }
        }
        auto &node = data.nodes[obj_to_node_index[&obj]];
        for(const auto &child: obj.children) { node.children.push_back(obj_to_node_index[child.get()]); }

        if(auto *mesh_component = obj.get_component_ptr<vierkant::mesh_component_t>())
        {
            mesh_state_t mesh_state = {mesh_component->mesh->id, mesh_component->entry_indices,
                                       mesh_component->library};
            node.mesh_state = mesh_state;

            // store materials with dirty hashes
            for(const auto &mat: mesh_component->mesh->materials)
            {
                if(mat->hash != std::hash<vierkant::material_t>()(mat->m)) { data.materials[mat->m.id] = mat->m; }
            }
        }
        return true;
    });

    save_scene_data(data, path);
}

void PBRViewer::build_scene(const std::optional<scene_data_t> &scene_data_in, bool clear_scene, SceneId scene_id)
{
    auto start_time = std::chrono::high_resolution_clock::now();

    auto load_task = [this, scene_data_in, scene_id, clear_scene, start_time]() {
        // load background
        if(scene_data_in && clear_scene) { load_file(scene_data_in->environment_path, false); }

        struct scene_data_assets_t
        {
            scene_data_t scene_data;
            SceneId scene_id;
            std::unordered_map<vierkant::MeshId, vierkant::MeshPtr> meshes;
            std::vector<vierkant::Object3DPtr> objects;
        };
        std::vector<scene_data_assets_t> scene_assets(1);

        if(scene_data_in)
        {
            scene_assets[0].scene_data = std::move(*scene_data_in);
            scene_assets[0].scene_id = scene_id;

            // sub-scenes
            std::deque<std::pair<SceneId, std::string>> sub_scene_paths = {
                    scene_assets[0].scene_data.scene_paths.begin(), scene_assets[0].scene_data.scene_paths.end()};

            // iterate subscene-paths bfs
            while(!sub_scene_paths.empty())
            {
                auto [id, p] = std::move(sub_scene_paths.front());
                sub_scene_paths.pop_front();

                m_scene_paths[id] = p;

                auto sub_scene_data = load_scene_data(p);
                if(sub_scene_data)
                {
                    auto &new_scene_asset = scene_assets.emplace_back();
                    new_scene_asset.scene_data = std::move(*sub_scene_data);
                    new_scene_asset.scene_id = id;

                    for(const auto &[id, sub_scene_path]: new_scene_asset.scene_data.scene_paths)
                    {
                        sub_scene_paths.emplace_back(id, sub_scene_path);
                    }
                }
            }
            // std::unordered_map<std::string, vierkant::MeshPtr> mesh_cache;
            std::unordered_map<std::string, std::future<vierkant::MeshPtr>> mesh_future_cache;

            // schedule background creation of meshes
            for(auto &asset: scene_assets)
            {
                for(const auto &[mesh_id, path]: asset.scene_data.model_paths)
                {
                    auto cache_it = mesh_future_cache.find(path);
                    if(cache_it != mesh_future_cache.end()) {}
                    else
                    {
                        mesh_future_cache[path] = background_queue().post([this, path] { return load_mesh(path); });
                    }
                }
            }

            // load meshes for scene and sub-scenes
            for(auto &asset: scene_assets)
            {
                for(const auto &[mesh_id, path]: asset.scene_data.model_paths)
                {
                    // sync and check
                    if(auto mesh = mesh_future_cache[path].get())
                    {
                        // optional material override(s)
                        for(auto &mat: mesh->materials)
                        {
                            auto it = asset.scene_data.materials.find(mat->m.id);
                            if(it != asset.scene_data.materials.end())
                            {
                                mat->m = it->second;
                                spdlog::trace("overriding material: {}", mat->m.name);
                            }
                        }
                        asset.meshes[mesh_id] = mesh;
                    }
                }
            }
        }
        else
        {
            auto cube_mesh = load_mesh("cube");
            scene_assets[0].meshes[cube_mesh->id] = cube_mesh;
            scene_node_t node = {};
            node.name = "cube";
            node.mesh_state = {vierkant::MeshId::from_name(node.name)};
            scene_assets[0].scene_data.nodes = {node};
            scene_assets[0].scene_data.scene_roots = {0};
            scene_assets[0].scene_id = scene_id;
            m_scene_paths[scene_id] = s_default_scene_path;
        }

        auto create_root_object = [this](const scene_data_t &scene_data,
                                         const std::unordered_map<vierkant::MeshId, vierkant::MeshPtr> &meshes,
                                         const std::shared_ptr<entt::registry> &registry,
                                         std::vector<vierkant::Object3DPtr> &out_objects) -> vierkant::Object3DPtr {
            if(!scene_data.nodes.empty())
            {
                auto root = m_object_store->create_object();
                root->name = scene_data.name;

                // create objects for all nodes
                for(const auto &node: scene_data.nodes)
                {
                    vierkant::Object3DPtr obj;

                    if(node.mesh_state && meshes.contains(node.mesh_state->mesh_id))
                    {
                        const auto &mesh = meshes.at(node.mesh_state->mesh_id);
                        obj = m_scene->create_mesh_object(
                                {mesh, node.mesh_state->entry_indices, node.mesh_state->mesh_library});
                    }
                    else { obj = m_object_store->create_object(); }
                    obj->name = node.name;
                    obj->enabled = node.enabled;
                    obj->transform = node.transform;
                    if(node.animation_state) { obj->add_component(*node.animation_state); }
                    if(node.physics_state) { obj->add_component(*node.physics_state); }

                    out_objects.push_back(obj);
                }

                // recreate node-hierarchy
                for(uint32_t i = 0; i < scene_data.nodes.size(); ++i)
                {
                    for(auto child_index: scene_data.nodes[i].children)
                    {
                        out_objects[i]->add_child(out_objects[child_index]);
                    }
                }

                // add scene-roots
                for(auto idx: scene_data.scene_roots) { root->add_child(out_objects[idx]); }

                for(const auto &cam: scene_data.cameras)
                {
                    auto object = std::visit(
                            [this, &registry](auto &&camera_params) -> vierkant::CameraPtr {
                                using T = std::decay_t<decltype(camera_params)>;

                                if constexpr(std::is_same_v<T, vierkant::ortho_camera_params_t>)
                                {
                                    return vierkant::OrthoCamera::create(registry, camera_params);
                                }
                                else if constexpr(std::is_same_v<T, vierkant::physical_camera_params_t>)
                                {
                                    return vierkant::PerspectiveCamera::create(registry, camera_params);
                                }
                                else
                                    return nullptr;
                            },
                            cam.params);

                    object->name = cam.name;
                    object->transform = cam.transform;
                    root->add_child(object);

                    m_camera = object;
                }
                return root;
            }
            return nullptr;
        };

        auto done_cb = [this, scene_assets = std::move(scene_assets), create_root_object, clear_scene,
                        start_time]() mutable {
            // root nodes for all (sub-)scenes
            std::vector<vierkant::Object3DPtr> root_objects(scene_assets.size());

            // map scene-ids to their root-objects
            std::unordered_map<SceneId, vierkant::Object3DPtr> scene_root_map;

            for(uint32_t i = 0; i < scene_assets.size(); ++i)
            {
                root_objects[i] = create_root_object(scene_assets[i].scene_data, scene_assets[i].meshes,
                                                     m_scene->registry(), scene_assets[i].objects);
                scene_root_map[scene_assets[i].scene_id] = root_objects[i];
                auto &cmp = root_objects[i]->add_component<object_flags_component_t>();
                cmp.scene_id = scene_assets[i].scene_id;
            }

            for(auto &scene_asset: scene_assets)
            {
                // connect sub-scenes to nodes
                for(uint32_t j = 0; j < scene_asset.scene_data.nodes.size(); ++j)
                {
                    const auto &node = scene_asset.scene_data.nodes[j];

                    if(node.scene_id)
                    {
                        assert(scene_root_map.contains(*node.scene_id));
                        auto children = scene_root_map[*node.scene_id]->children;
                        for(const auto &child: children)
                        {
                            scene_asset.objects[j]->add_child(m_object_store->clone(child.get()));
                        }

                        // flag object to contain a sub-scene
                        auto &cmp = scene_asset.objects[j]->add_component<object_flags_component_t>();
                        cmp.scene_id = *node.scene_id;
                    }
                }
            }

            if(root_objects[0])
            {
                if(clear_scene)
                {
                    m_scene->clear();
                    auto children = root_objects[0]->children;
                    for(const auto &child: children) { m_scene->add_object(child); }
                    m_scene_id = scene_assets[0].scene_id;
                }
                else { m_scene->add_object(root_objects[0]); }
            }
            if(m_path_tracer) { m_path_tracer->reset_accumulator(); }

            // log timing
            auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::high_resolution_clock::now() - start_time)
                                    .count() /
                            1000.f;
            spdlog::debug("done building scene ({:.2f} s): {}", build_ms, m_scene_paths[m_scene_id].string());
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

    if(path == "cube")
    {
        mesh = m_box_mesh;
        mesh->id = vierkant::MeshId::from_name("cube");
    }
    else if(!path.empty())
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
        auto [m, textures, samplers] = vierkant::model::load_mesh(load_params, *model_assets);
        mesh = m;
        mesh->id = mesh_id;

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

    // store mesh/path
    m_model_paths[mesh->id] = path;
    return mesh;
}

bool PBRViewer::parse_override_settings(int argc, char *argv[])
{
    // available options
    cxxopts::Options options(argv[0], "3d-model viewer with rasterization and path-tracer backends\n");
    options.positional_help("[<model-file>] [<hdr-image>]");
    options.add_options()("help", "print this help message");
    options.add_options()("w,width", "window width in px", cxxopts::value<uint32_t>());
    options.add_options()("h,height", "window height in px", cxxopts::value<uint32_t>());
    options.add_options()("v,verbose", "verbose printing");
    options.add_options()("q,quiet", "minimal printing");
    options.add_options()("log-file", "enable logging to a file", cxxopts::value<std::string>());
    options.add_options()("f,fullscreen", "enable fullscreen");
    options.add_options()("no-fullscreen", "disable fullscreen");
    options.add_options()("vsync", "enable vsync");
    options.add_options()("no-vsync", "disable vsync");
    options.add_options()("hdr", "enable hdr");
    options.add_options()("no-hdr", "disable hdr");
    options.add_options()("font", "provide a font-file (.ttf | .otf)", cxxopts::value<std::string>());
    options.add_options()("font-size", "provide a font-size", cxxopts::value<float>());
    options.add_options()("validation", "enable vulkan validation");
    options.add_options()("no-validation", "disable vulkan validation");
    options.add_options()("l,labels", "enable vulkan debug-labels");
    options.add_options()("no-labels", "disable vulkan debug-labels");
    options.add_options()("raytracing", "enable vulkan raytracing extensions");
    options.add_options()("no-raytracing", "disable vulkan raytracing extensions");
    options.add_options()("mesh-shader", "enable vulkan mesh-shader extensions");
    options.add_options()("no-mesh-shader", "disable vulkan mesh-shader extensions");
    options.add_options()("files", "provided input files", cxxopts::value<std::vector<std::string>>());
    options.parse_positional("files");

    cxxopts::ParseResult result;

    try
    {
        result = options.parse(argc, argv);
    } catch(std::exception &e)
    {
        spdlog::error(e.what());
        return false;
    }

    if(result.count("files"))
    {
        const auto &files = result["files"].as<std::vector<std::string>>();

        for(const auto &f: files)
        {
            switch(crocore::filesystem::get_file_type(f))
            {
                case crocore::filesystem::FileType::IMAGE: m_scene_data.environment_path = f; break;

                case crocore::filesystem::FileType::MODEL:
                {
                    vierkant::MeshId mesh_id;
                    m_scene_data.model_paths = {{mesh_id, f}};
                    scene_node_t node = {};
                    node.name = std::filesystem::path(f).filename().string();
                    node.mesh_state = {mesh_id};
                    m_scene_data.nodes = {node};
                    m_scene_data.scene_roots = {0};
                    break;
                }

                default:
                {
                    if(auto scene_data = load_scene_data(f)) { m_scene_data = *scene_data; }
                }
                break;
            }
        }
    }

    // print usage
    if(result.count("help"))
    {
        spdlog::set_pattern("%v");
        spdlog::info("\n{}", options.help());
        return false;
    }
    if(result.count("width")) { m_settings.window_info.size.x = (int) result["width"].as<uint32_t>(); }
    if(result.count("height")) { m_settings.window_info.size.y = (int) result["height"].as<uint32_t>(); }
    if(result.count("log-file")) { m_settings.log_file = result["log-file"].as<std::string>(); }
    if(result.count("fullscreen")) { m_settings.window_info.fullscreen = true; }
    if(result.count("no-fullscreen")) { m_settings.window_info.fullscreen = false; }
    if(result.count("vsync")) { m_settings.window_info.vsync = true; }
    if(result.count("no-vsync")) { m_settings.window_info.vsync = false; }
    if(result.count("hdr")) { m_settings.window_info.use_hdr = true; }
    if(result.count("no-hdr")) { m_settings.window_info.use_hdr = false; }
    if(result.count("font")) { m_settings.font_url = result["font"].as<std::string>(); }
    if(result.count("font-size")) { m_settings.ui_font_scale = result["font-size"].as<float>(); }
    if(result.count("validation")) { m_settings.use_validation = true; }
    if(result.count("no-validation")) { m_settings.use_validation = false; }
    if(result.count("labels")) { m_settings.use_debug_labels = true; }
    if(result.count("no-labels")) { m_settings.use_debug_labels = false; }
    if(result.count("verbose")) { m_settings.log_level = spdlog::level::debug; }
    if(result.count("quiet")) { m_settings.log_level = spdlog::level::info; }
    if(result.count("raytracing"))
    {
        m_settings.enable_ray_query_features = true;
        m_settings.enable_raytracing_pipeline_features = true;
    }
    if(result.count("no-raytracing"))
    {
        m_settings.enable_ray_query_features = false;
        m_settings.enable_raytracing_pipeline_features = false;
    }
    if(result.count("mesh-shader")) { m_settings.enable_mesh_shader_device_features = true; }
    if(result.count("no-mesh-shader")) { m_settings.enable_mesh_shader_device_features = false; }
    spdlog::set_level(m_settings.log_level);
    return true;
}