#include <fstream>

#include <crocore/filesystem.hpp>
#include <crocore/http.hpp>
#include <crocore/Image.hpp>

#include <vierkant/imgui/imgui_util.h>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/cubemap_utils.hpp>
#include "ziparchive.h"

#include <vierkant/gltf.hpp>
#include <vierkant/Visitor.hpp>

#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include "pbr_viewer.hpp"

////////////////////////////// VALIDATION LAYER ///////////////////////////////////////////////////

#ifdef NDEBUG
const bool g_enable_validation_layers = false;
#else
const bool g_enable_validation_layers = true;
#endif

constexpr char g_zip_path[] = "asset_bundle_cache.zip";

using double_second = std::chrono::duration<double>;

using log_delegate_fn_t = std::function<void(const std::string &msg,
                                             spdlog::level::level_enum log_level,
                                             const std::string &logger_name)>;

class delegate_sink_t : public spdlog::sinks::base_sink<std::mutex>
{
public:
    std::unordered_map<std::string, log_delegate_fn_t> log_delegates;

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override
    {
        // log_msg is a struct containing the log entry info like level, timestamp, thread id etc.
        // msg.raw contains pre formatted log

        // If needed (very likely but not mandatory), the sink formats the message before sending it to its final destination:
        spdlog::memory_buf_t formatted;
        spdlog::sinks::base_sink<std::mutex>::formatter_->format(msg, formatted);

        // bounce out via delegates
        for(const auto &[name, delegate]: log_delegates)
        {
            if(delegate)
            {
                delegate(fmt::to_string(formatted), msg.level, std::string(msg.logger_name.begin(),
                                                                           msg.logger_name.end()));
            }
        }
    }

    void flush_() override{}
};

PBRViewer::PBRViewer(const crocore::Application::create_info_t &create_info) : crocore::Application(create_info)
{
    // create logger for renderers
    constexpr char pbr_logger_name[] = "pbr_deferred";
    _loggers[pbr_logger_name] = spdlog::stdout_color_mt(pbr_logger_name);
    _loggers[spdlog::default_logger()->name()] = spdlog::default_logger();

    auto scroll_log_sink = std::make_shared<delegate_sink_t>();
    scroll_log_sink->log_delegates[name()] = [this](const std::string &msg,
                                                    spdlog::level::level_enum log_level,
                                                    const std::string &/*logger_name*/)
    {
        std::unique_lock lock(m_log_queue_mutex);
        m_log_queue.emplace_back(msg, log_level);
        while(m_log_queue.size() > m_max_log_queue_size){ m_log_queue.pop_front(); }
    };
    for(auto &[name, logger]: _loggers){ logger->sinks().push_back(scroll_log_sink); }

    // try to read settings
    m_settings = load_settings();

    spdlog::set_level(m_settings.log_level);
    this->loop_throttling = !m_settings.window_info.vsync;
    this->target_loop_frequency = m_settings.target_fps;

    for(const auto &path: create_info.arguments)
    {
        switch(crocore::filesystem::get_file_type(path))
        {
            case crocore::filesystem::FileType::IMAGE:m_settings.environment_path = path;
                break;

            case crocore::filesystem::FileType::MODEL:m_settings.model_path = path;
                break;

            default:break;
        }
    }
}

void PBRViewer::setup()
{
    create_context_and_window();

    // create ui and inputs
    create_ui();

    create_texture_image();
    create_graphics_pipeline();

    // load stuff
    if(m_settings.model_path.empty()){ load_model(); }
    else{ load_file(m_settings.model_path); }
    load_file(m_settings.environment_path);
}

void PBRViewer::teardown()
{
    spdlog::debug("joining background tasks ...");
    background_queue().join_all();
    main_queue().poll();
    vkDeviceWaitIdle(m_device->handle());
    spdlog::info("ciao {}", name());
}

void PBRViewer::poll_events()
{
    if(m_window){ m_window->poll_events(); }
}

void PBRViewer::create_context_and_window()
{
    m_instance = vierkant::Instance(g_enable_validation_layers, vierkant::Window::required_extensions());

    m_settings.window_info.title = name();
    m_settings.window_info.instance = m_instance.handle();
    m_window = vierkant::Window::create(m_settings.window_info);

    VkPhysicalDevice physical_device = m_instance.physical_devices().front();

    for(const auto &pd: m_instance.physical_devices())
    {
        VkPhysicalDeviceProperties device_props = {};
        vkGetPhysicalDeviceProperties(pd, &device_props);

        if(device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physical_device = pd;
            break;
        }
    }

    // create device
    vierkant::Device::create_info_t device_info = {};
    device_info.instance = m_instance.handle();
    device_info.physical_device = physical_device;
    device_info.use_validation = m_instance.use_validation_layers();
    device_info.surface = m_window->surface();

    // check raytracing support
    m_settings.enable_raytracing_device_features =
            m_settings.enable_raytracing_device_features &&
            vierkant::check_device_extension_support(physical_device, vierkant::RayTracer::required_extensions());

    // add the raytracing-extensions
    if(m_settings.enable_raytracing_device_features)
    {
        device_info.use_raytracing = true;
        device_info.extensions = vierkant::RayTracer::required_extensions();
    }

    // check mesh-shader support
    m_settings.enable_mesh_shader_device_features =
            m_settings.enable_mesh_shader_device_features &&
            vierkant::check_device_extension_support(physical_device, {VK_EXT_MESH_SHADER_EXTENSION_NAME});

    if(m_settings.enable_mesh_shader_device_features)
    {
        device_info.extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }

    m_device = vierkant::Device::create(device_info);
    m_window->create_swapchain(m_device, std::min(m_device->max_usable_samples(), m_settings.window_info.sample_count),
                               m_settings.window_info.vsync);

    // create a WindowDelegate
    vierkant::window_delegate_t window_delegate = {};
    window_delegate.draw_fn = [this](const vierkant::WindowPtr &w){ return draw(w); };
    window_delegate.resize_fn = [this](uint32_t w, uint32_t h)
    {
        VkViewport viewport = {0.f, 0.f, static_cast<float>(w), static_cast<float>(h), 0.f, 1.f};
        m_renderer.viewport = m_renderer_overlay.viewport = m_renderer_gui.viewport = viewport;
        m_renderer.sample_count = m_renderer_overlay.sample_count =
        m_renderer_gui.sample_count = m_window->swapchain().sample_count();
        m_camera->get_component<vierkant::projective_camera_params_t>().aspect = m_window->aspect_ratio();
        m_camera_control.current->screen_size = {w, h};
    };
    window_delegate.close_fn = [this](){ running = false; };
    m_window->window_delegates[name()] = window_delegate;

    // create a draw context
    m_draw_context = vierkant::DrawContext(m_device);

    m_pipeline_cache = vierkant::PipelineCache::create(m_device);

    // set some separate queues for background stuff
    m_queue_model_loading = m_device->queues(vierkant::Device::Queue::GRAPHICS)[1];
    m_queue_image_loading = m_device->queues(vierkant::Device::Queue::GRAPHICS)[2];
    m_queue_pbr_render = m_device->queues(vierkant::Device::Queue::GRAPHICS)[3];
    m_queue_path_tracer = m_device->queues(vierkant::Device::Queue::GRAPHICS)[4];
}

void PBRViewer::create_graphics_pipeline()
{
    m_pipeline_cache->clear();

    bool use_raytracer = m_scene_renderer ? m_scene_renderer == m_path_tracer : m_settings.path_tracing;

    const auto &framebuffers = m_window->swapchain().framebuffers();
    auto fb_extent = framebuffers.front().extent();

    vierkant::Renderer::create_info_t create_info = {};
    create_info.num_frames_in_flight = framebuffers.size();
    create_info.sample_count = m_window->swapchain().sample_count();
    create_info.viewport.width = static_cast<float>(fb_extent.width);
    create_info.viewport.height = static_cast<float>(fb_extent.height);
    create_info.viewport.maxDepth = static_cast<float>(fb_extent.depth);
    create_info.pipeline_cache = m_pipeline_cache;

    m_renderer = vierkant::Renderer(m_device, create_info);
    m_renderer_overlay = vierkant::Renderer(m_device, create_info);
    m_renderer_overlay.indirect_draw = true;

    m_renderer_gui = vierkant::Renderer(m_device, create_info);

    vierkant::PBRDeferred::create_info_t pbr_render_info = {};
    pbr_render_info.queue = m_queue_pbr_render;
    pbr_render_info.num_frames_in_flight = framebuffers.size();
    pbr_render_info.pipeline_cache = m_pipeline_cache;
    pbr_render_info.settings = m_settings.pbr_settings;
    pbr_render_info.settings.use_meshlet_pipeline = m_settings.enable_mesh_shader_device_features;
    pbr_render_info.logger_name = "pbr_deferred";

    if(m_pbr_renderer)
    {
        pbr_render_info.conv_lambert = m_pbr_renderer->environment_lambert();
        pbr_render_info.conv_ggx = m_pbr_renderer->environment_ggx();
        pbr_render_info.brdf_lut = m_pbr_renderer->bsdf_lut();
        pbr_render_info.settings = m_pbr_renderer->settings;
    }
    m_pbr_renderer = vierkant::PBRDeferred::create(m_device, pbr_render_info);

    if(m_settings.enable_raytracing_device_features)
    {
        vierkant::PBRPathTracer::create_info_t path_tracer_info = {};
        path_tracer_info.num_frames_in_flight = framebuffers.size();
        path_tracer_info.pipeline_cache = m_pipeline_cache;

        path_tracer_info.settings = m_path_tracer ? m_path_tracer->settings : m_settings.path_tracer_settings;
        path_tracer_info.queue = m_queue_path_tracer;

        m_path_tracer = vierkant::PBRPathTracer::create(m_device, path_tracer_info);
    }

    if(use_raytracer){ m_scene_renderer = m_path_tracer; }
    else{ m_scene_renderer = m_pbr_renderer; }
}

void PBRViewer::create_texture_image()
{
    // try to fetch cool image
    auto http_response = crocore::net::http::get(g_texture_url);

    crocore::ImagePtr img;
    vierkant::Image::Format fmt;

    // create from downloaded data
    if(!http_response.data.empty()){ img = crocore::create_image_from_data(http_response.data, 4); }
    else
    {
        // create 4x4 black/white checkerboard image
        uint32_t v[] = {0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000,
                        0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF,
                        0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF, 0xFF000000,
                        0xFF000000, 0xFFFFFFFF, 0xFF000000, 0xFFFFFFFF};

        img = crocore::Image_<uint8_t>::create(reinterpret_cast<uint8_t *>(v), 4, 4, 4);
        fmt.mag_filter = VK_FILTER_NEAREST;
        fmt.format = VK_FORMAT_R8G8B8A8_UNORM;
    }
    fmt.extent = {img->width(), img->height(), 1};
    fmt.use_mipmap = true;
    m_textures["test"] = vierkant::Image::create(m_device, img->data(), fmt);
}

void PBRViewer::load_model(const std::string &path)
{
    vierkant::MeshPtr mesh;

    // additionally required buffer-flags for raytracing/compute/mesh-shading
    VkBufferUsageFlags buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if(m_settings.enable_raytracing_device_features)
    {
        buffer_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    if(!path.empty())
    {
        m_settings.model_path = path;

        auto load_task = [this, path, buffer_flags]()
        {
            m_num_loading++;
            auto start_time = std::chrono::steady_clock::now();

            spdlog::debug("loading model '{}'", path);

            // tinygltf
            auto scene_assets = vierkant::model::gltf(path);
            spdlog::debug("loaded model '{}' ({})", path, double_second(std::chrono::steady_clock::now() - start_time));

            if(scene_assets.entry_create_infos.empty())
            {
                spdlog::warn("could not load file: {}", path);
                return;
            }

            // lookup
            std::optional<vierkant::model::asset_bundle_t> bundle;
            size_t hash_val = std::hash<std::string>()(path);
            crocore::hash_combine(hash_val, m_settings.optimize_vertex_cache);
            crocore::hash_combine(hash_val, m_settings.generate_lods);
            crocore::hash_combine(hash_val, m_settings.generate_meshlets);
            std::filesystem::path bundle_path = std::to_string(hash_val) + ".bin";

            bool bundle_created = false;
            bundle = load_asset_bundle(bundle_path);

            if(!bundle)
            {
                spdlog::stopwatch sw;
                spdlog::debug("creating mesh-bundle ...");

                vierkant::create_mesh_buffers_params_t params = {};
                params.optimize_vertex_cache = m_settings.optimize_vertex_cache;
                params.generate_lods = m_settings.generate_lods;
                params.generate_meshlets = m_settings.generate_meshlets;
                params.use_vertex_colors = false;
                params.pack_vertices = true;

                vierkant::model::asset_bundle_t asset_bundle;
                asset_bundle.mesh_buffer_bundle = vierkant::create_mesh_buffers(scene_assets.entry_create_infos,
                                                                                params);

                if(m_settings.texture_compression)
                {
                    asset_bundle.compressed_images = vierkant::model::create_compressed_images(scene_assets.materials);
                }
                bundle = std::move(asset_bundle);
                spdlog::debug("mesh-bundle done ({})", sw.elapsed());
                bundle_created = true;
            }

            vierkant::model::load_mesh_params_t load_params = {};
            load_params.device = m_device;
            load_params.load_queue = m_queue_model_loading;
            load_params.compress_textures = m_settings.texture_compression;
            load_params.optimize_vertex_cache = m_settings.optimize_vertex_cache;
            load_params.generate_lods = m_settings.generate_lods;
            load_params.generate_meshlets = m_settings.generate_meshlets;
            load_params.buffer_flags = buffer_flags;
            auto mesh = load_mesh(load_params, scene_assets, bundle);

            if(!mesh)
            {
                spdlog::warn("loading '{}' failed ...", path);
                m_num_loading--;
                return;
            }

            if(bundle_created && m_settings.cache_mesh_bundles)
            {
                background_queue().post([this, bundle = std::move(bundle), bundle_path]()
                                        {
                                            save_asset_bundle(*bundle, bundle_path);
                                        });
            }


            auto done_cb = [this, mesh, lights = std::move(scene_assets.lights), start_time, path]()
            {
                m_selected_objects.clear();
                m_scene->clear();

                for(const auto &l: lights)
                {
                    auto light_object = vierkant::Object3D::create(m_scene->registry());
                    light_object->name = fmt::format("light_{}", light_object->id());
                    light_object->add_component(l);
                    m_scene->add_object(light_object);
                }

                // tmp test-loop
                for(uint32_t i = 0; i < 1; ++i)
                {
                    auto object = vierkant::create_mesh_object(m_scene->registry(), mesh);

                    // scale
                    float scale = 5.f / glm::length(object->aabb().half_extents());
                    object->set_scale(scale);

                    // center aabb
                    auto aabb = object->aabb().transform(object->transform);
                    object->set_position(-aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 3.f * (float) i));

                    m_scene->add_object(object);
                }

                if(m_path_tracer){ m_path_tracer->reset_accumulator(); }

                auto dur = double_second(std::chrono::steady_clock::now() - start_time);
                spdlog::debug("loaded '{}' -- ({:03.2f})", path, dur.count());
                m_num_loading--;
            };
            main_queue().post(done_cb);
        };
        background_queue().post(load_task);
    }
    else
    {
        auto box = vierkant::Geometry::Box(glm::vec3(.5f));
        box->colors.clear();

        vierkant::Mesh::create_info_t mesh_create_info = {};
        mesh_create_info.optimize_vertex_cache = true;
        mesh_create_info.generate_meshlets = true;
        mesh_create_info.use_vertex_colors = false;
        mesh_create_info.pack_vertices = true;
        mesh_create_info.buffer_usage_flags = buffer_flags;
        mesh = vierkant::Mesh::create_from_geometry(m_device, box, mesh_create_info);
        auto mat = vierkant::Material::create();

        auto it = m_textures.find("test");
        if(it != m_textures.end()){ mat->textures[vierkant::Material::Color] = it->second; }
        mesh->materials = {mat};

        auto mesh_node = vierkant::create_mesh_object(m_scene->registry(), mesh);

        // center aabb
        auto aabb = mesh_node->aabb().transform(mesh_node->transform);
        mesh_node->set_position(-aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 0.f));

        m_selected_objects.clear();
        m_scene->add_object(mesh_node);
    }
}

void PBRViewer::load_environment(const std::string &path)
{
    auto load_task = [&, path]()
    {
        m_num_loading++;

        auto start_time = std::chrono::steady_clock::now();

        auto img = crocore::create_image_from_file(path, 4);

        vierkant::ImagePtr panorama, skybox, conv_lambert, conv_ggx;

        if(img)
        {
            constexpr VkFormat hdr_format = VK_FORMAT_B10G11R11_UFLOAT_PACK32;

            bool use_float = (img->num_bytes() /
                              (img->width() * img->height() * img->num_components())) > 1;

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
                float res = static_cast<float>(crocore::next_pow_2(std::max(img->width(), img->height()) / 4));
                skybox = vierkant::cubemap_from_panorama(panorama, {res, res}, m_queue_image_loading, true, hdr_format);
            }

            if(skybox)
            {
                constexpr uint32_t lambert_size = 128;
                conv_lambert = vierkant::create_convolution_lambert(m_device, skybox, lambert_size,
                                                                    hdr_format,
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

        main_queue().post([this, path, skybox, conv_lambert, conv_ggx, start_time]()
                          {
                              m_scene->set_environment(skybox);

                              m_pbr_renderer->set_environment(conv_lambert, conv_ggx);

                              if(m_path_tracer){ m_path_tracer->reset_accumulator(); }

                              m_settings.environment_path = path;

                              auto dur = double_second(std::chrono::steady_clock::now() - start_time);
                              spdlog::debug("loaded '{}' -- ({:03.2f})", path, dur.count());
                              m_num_loading--;
                          });
    };
    background_queue().post(load_task);
}

void PBRViewer::update(double time_delta)
{
    m_camera_control.current->update(time_delta);

    // update animated objects in the scene
    m_scene->update(time_delta);

    // issue top-level draw-command
    m_window->draw();
}

vierkant::window_delegate_t::draw_result_t PBRViewer::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = m_window->swapchain().framebuffers()[image_index];

    std::vector<vierkant::semaphore_submit_info_t> semaphore_infos;

    auto render_scene = [this, &framebuffer, &semaphore_infos]() -> VkCommandBuffer
    {
        auto render_result = m_scene_renderer->render_scene(m_renderer, m_scene, m_camera, {});
        semaphore_infos = render_result.semaphore_infos;
        return m_renderer.render(framebuffer);
    };

    auto render_scene_overlays = [this, &framebuffer]() -> VkCommandBuffer
    {
        for(const auto &obj: m_selected_objects)
        {
            auto modelview = m_camera->view_matrix() * obj->transform;

            if(m_settings.draw_aabbs)
            {
                m_draw_context.draw_boundingbox(m_renderer_overlay, obj->aabb(), modelview,
                                                m_camera->projection_matrix());

                auto sub_aabbs = obj->sub_aabbs();

                for(const auto &aabb: sub_aabbs)
                {
                    m_draw_context.draw_boundingbox(m_renderer_overlay, aabb, modelview, m_camera->projection_matrix());
                }
            }

            if(m_settings.draw_node_hierarchy)
            {
                vierkant::nodes::node_animation_t animation = {};

                if(obj->has_component<vierkant::animation_state_t>())
                {
                    const auto &mesh = obj->get_component<vierkant::MeshPtr>();

                    auto &animation_state = obj->get_component<vierkant::animation_state_t>();
                    animation = mesh->node_animations[animation_state.index];
                    auto node = mesh->root_bone ? mesh->root_bone : mesh->root_node;
                    m_draw_context.draw_node_hierarchy(m_renderer_overlay, node, animation,
                                                       static_cast<float>(animation_state.current_time),
                                                       modelview, m_camera->projection_matrix());
                }
            }
        }

        if(m_settings.draw_grid)
        {
            m_draw_context.draw_grid(m_renderer_overlay, 10.f, 100, m_camera->view_matrix(),
                                     m_camera->projection_matrix());
        }

        return m_renderer_overlay.render(framebuffer);
    };

    auto render_gui = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_renderer_gui);
        return m_renderer_gui.render(framebuffer);
    };

    vierkant::window_delegate_t::draw_result_t ret;

    // submit and wait for all command-creation tasks to complete
    std::vector<std::future<VkCommandBuffer>> cmd_futures;
    cmd_futures.push_back(background_queue().post(render_scene));
    cmd_futures.push_back(background_queue().post(render_scene_overlays));
    if(m_settings.draw_ui){ cmd_futures.push_back(background_queue().post(render_gui)); }
    crocore::wait_all(cmd_futures);

    // get values from completed futures
    for(auto &f: cmd_futures)
    {
        VkCommandBuffer commandbuffer = f.get();
        if(commandbuffer){ ret.command_buffers.push_back(commandbuffer); }
    }

    // get semaphore infos
    ret.semaphore_infos = std::move(semaphore_infos);
    return ret;
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
    settings.camera_params = m_camera->get_component<vierkant::projective_camera_params_t>();

    // renderer settings
    settings.pbr_settings = m_pbr_renderer->settings;
    if(m_path_tracer){ settings.path_tracer_settings = m_path_tracer->settings; }
    settings.path_tracing = m_scene_renderer == m_path_tracer;

    // create and open a character archive for output
    std::ofstream ofs(path.string());

    // save data to archive
    try
    {
        cereal::JSONOutputArchive archive(ofs);

        // write class instance to archive
        archive(settings);

    } catch(std::exception &e){ spdlog::error(e.what()); }

    spdlog::debug("save settings: {}", path.string());
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
        } catch(std::exception &e){ spdlog::error(e.what()); }

        spdlog::debug("loading settings: {}", path.string());
    }
    return settings;
}

void PBRViewer::load_file(const std::string &path)
{
    auto add_to_recent_files = [this](const std::string &f)
    {
        main_queue().post([this, f]
                          {
                              m_settings.recent_files.push_back(f);
                              while(m_settings.recent_files.size() > 10){ m_settings.recent_files.pop_front(); }
                          });
    };

    switch(crocore::filesystem::get_file_type(path))
    {
        case crocore::filesystem::FileType::IMAGE:add_to_recent_files(path);
            load_environment(path);
            break;

        case crocore::filesystem::FileType::MODEL:add_to_recent_files(path);
            load_model(path);
            break;

        default:break;
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
    }
    catch(std::exception &e){ spdlog::error(e.what()); }
}

std::optional<vierkant::model::asset_bundle_t>
PBRViewer::load_asset_bundle(const std::filesystem::path &path)
{
    vierkant::ziparchive zip(g_zip_path);

    if(zip.has_file(path))
    {
        try
        {
            spdlog::debug("loading bundle '{}' from archive '{}'", path.string(), g_zip_path);
            vierkant::model::asset_bundle_t ret;

            std::shared_lock lock(m_bundle_rw_mutex);
            auto zipstream = zip.open_file(path);
            cereal::BinaryInputArchive archive(zipstream);
            archive(ret);
            return ret;
        }
        catch(std::exception &e){ spdlog::error(e.what()); }
    }
    return {};
}

int main(int argc, char *argv[])
{
    crocore::Application::create_info_t create_info = {};
    create_info.arguments = {argv, argv + argc};
    create_info.num_background_threads = 4;//std::thread::hardware_concurrency();

    auto app = std::make_shared<PBRViewer>(create_info);
    return app->run();
}