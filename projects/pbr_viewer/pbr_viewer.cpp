#include <fstream>

#include <crocore/filesystem.hpp>
#include <crocore/http.hpp>
#include <crocore/Image.hpp>
#include <crocore/json.hpp>

#include <vierkant/imgui/imgui_util.h>
#include <vierkant/Visitor.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/cubemap_utils.hpp>

#include <vierkant/assimp.hpp>
#include <vierkant/gltf.hpp>
#include <vierkant/bc7.hpp>

#include "pbr_viewer.hpp"

////////////////////////////// VALIDATION LAYER ///////////////////////////////////////////////////

#ifdef NDEBUG
const bool g_enable_validation_layers = false;
#else
const bool g_enable_validation_layers = true;
#endif

using double_second = std::chrono::duration<double>;

void PBRViewer::setup()
{
    // try to read settings
    m_settings = load_settings();

    crocore::g_logger.set_severity(m_settings.log_severity);

    create_context_and_window();

    // create ui and inputs
    create_ui();

    create_texture_image();
    create_graphics_pipeline();

    // load stuff
    load_model(m_settings.model_path);
    load_environment(m_settings.environment_path);
}

void PBRViewer::teardown()
{
    LOG_INFO << "ciao " << name();

    background_queue().join_all();
    vkDeviceWaitIdle(m_device->handle());
}

void PBRViewer::poll_events()
{
    glfwPollEvents();
}

void PBRViewer::create_context_and_window()
{
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::required_extensions());

    // attach logger for debug-output
    m_instance.set_debug_fn([](const char *msg){ LOG_WARNING << msg; });

    m_settings.window_info.title = name();
    m_settings.window_info.instance = m_instance.handle();
    m_window = vk::Window::create(m_settings.window_info);

    // create device
    vk::Device::create_info_t device_info = {};
    device_info.instance = m_instance.handle();
    device_info.physical_device = m_instance.physical_devices().front();
    device_info.use_validation = m_instance.use_validation_layers();
    device_info.surface = m_window->surface();

    // add the raytracing-extensions
    device_info.extensions = vierkant::RayTracer::required_extensions();

    m_device = vk::Device::create(device_info);
    m_window->create_swapchain(m_device, std::min(m_device->max_usable_samples(), m_settings.window_info.sample_count),
                               m_settings.window_info.vsync);

    // create a WindowDelegate
    vierkant::window_delegate_t window_delegate = {};
    window_delegate.draw_fn = [this](const vierkant::WindowPtr &w){ return draw(w); };
    window_delegate.resize_fn = [this](uint32_t w, uint32_t h)
    {
        create_graphics_pipeline();
        m_camera->set_aspect(m_window->aspect_ratio());
        m_camera_control.current->screen_size = {w, h};
    };
    window_delegate.close_fn = [this](){ set_running(false); };
    m_window->window_delegates[name()] = window_delegate;

    // create a draw context
    m_draw_context = vierkant::DrawContext(m_device);

    m_pipeline_cache = vk::PipelineCache::create(m_device);

    // set some separate queues for background stuff
    m_queue_loading = m_device->queues(vierkant::Device::Queue::GRAPHICS)[1];
    m_queue_path_tracer = m_device->queues(vierkant::Device::Queue::GRAPHICS)[2];
}

void PBRViewer::create_ui()
{
    // create a KeyDelegate
    vierkant::key_delegate_t key_delegate = {};
    key_delegate.key_press = [this](const vierkant::KeyEvent &e)
    {
        if(!(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureKeyboard))
        {
            switch(e.code())
            {
                case vierkant::Key::_ESCAPE:
                    set_running(false);
                    break;

                case vierkant::Key::_C:
                    if(m_camera_control.current == m_camera_control.orbit)
                    {
                        m_camera_control.current = m_camera_control.fly;
                    }
                    else{ m_camera_control.current = m_camera_control.orbit; }
                    break;

                case vierkant::Key::_G:
                    m_pbr_renderer->settings.draw_grid = !m_pbr_renderer->settings.draw_grid;
                    break;

                case vierkant::Key::_P:
                    if(m_scene_renderer == m_pbr_renderer){ m_scene_renderer = m_path_tracer; }
                    else{ m_scene_renderer = m_pbr_renderer; }
                    break;

                case vierkant::Key::_B:
                    m_settings.draw_aabbs = !m_settings.draw_aabbs;
                    break;

                case vk::Key::_N:
                    m_settings.draw_node_hierarchy = !m_settings.draw_node_hierarchy;
                    break;

                case vk::Key::_S:
                    save_settings(m_settings);
                    break;
                default:
                    break;
            }
        }
    };
    m_window->key_delegates[name()] = key_delegate;

    // try to fetch a font from google-fonts
    auto http_response = crocore::net::http::get(g_font_url);

    m_font = vk::Font::create(m_device, http_response.data, 64);

    // create a gui and add a draw-delegate
    vk::gui::Context::create_info_t gui_create_info = {};
    gui_create_info.ui_scale = 2.f;
    gui_create_info.font_data = http_response.data;
    gui_create_info.font_size = 23.f;
    m_gui_context = vk::gui::Context(m_device, gui_create_info);

    m_gui_context.delegates["application"] = [this]
    {
        vk::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
    };

    // renderer window
    m_gui_context.delegates["renderer"] = [this]
    {
        bool is_path_tracer = m_scene_renderer == m_path_tracer;

        ImGui::Begin("renderer");

        if(ImGui::RadioButton("pbr-deferred", !is_path_tracer)){ m_scene_renderer = m_pbr_renderer; }
        ImGui::SameLine();
        if(ImGui::RadioButton("pathtracer", is_path_tracer)){ m_scene_renderer = m_path_tracer; }
        ImGui::Separator();

        vk::gui::draw_scene_renderer_ui(m_scene_renderer, m_camera);

        ImGui::End();
    };

    // scenegraph window
    m_gui_context.delegates["scenegraph"] = [this]{ vk::gui::draw_scene_ui(m_scene, m_camera, &m_selected_objects); };

    // imgui demo window
    m_gui_context.delegates["demo"] = []{ if(DEMO_GUI){ ImGui::ShowDemoWindow(&DEMO_GUI); }};

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    m_camera = vk::PerspectiveCamera::create(m_window->aspect_ratio(), 45.f, .1f, 100.f);

    create_camera_controls();

    vierkant::mouse_delegate_t simple_mouse = {};
    simple_mouse.mouse_press = [this](const vierkant::MouseEvent &e)
    {
        if(!(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse))
        {
            if(e.is_right()){ m_selected_objects.clear(); }
            else if(e.is_left())
            {
                auto picked_object = m_scene->pick(m_camera->calculate_ray(e.position(), m_window->size()));

                if(picked_object)
                {
                    if(e.is_control_down()){ m_selected_objects.insert(picked_object); }
                    else{ m_selected_objects = {picked_object}; }
                }
            }
        }
    };

    m_window->mouse_delegates["simple_mouse"] = simple_mouse;

    // attach drag/drop mouse-delegate
    vierkant::mouse_delegate_t file_drop_delegate = {};
    file_drop_delegate.file_drop = [this](const vierkant::MouseEvent &e, const std::vector<std::string> &files)
    {
        auto &f = files.back();

        switch(crocore::filesystem::get_file_type(f))
        {
            case crocore::filesystem::FileType::IMAGE:
                load_environment(f);
                break;

            case crocore::filesystem::FileType::MODEL:
                load_model(f);
                break;

            default:
                break;
        }
    };
    m_window->mouse_delegates["filedrop"] = file_drop_delegate;
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
    create_info.viewport.width = fb_extent.width;
    create_info.viewport.height = fb_extent.height;
    create_info.viewport.maxDepth = fb_extent.depth;
    create_info.pipeline_cache = m_pipeline_cache;

    m_renderer = vk::Renderer(m_device, create_info);
    m_renderer_overlay = vk::Renderer(m_device, create_info);
    m_renderer_gui = vk::Renderer(m_device, create_info);

    vierkant::PBRDeferred::create_info_t pbr_render_info = {};
    pbr_render_info.num_frames_in_flight = framebuffers.size();
    pbr_render_info.size = fb_extent;
    pbr_render_info.pipeline_cache = m_pipeline_cache;
    pbr_render_info.settings = m_settings.pbr_settings;

    if(m_pbr_renderer)
    {
        pbr_render_info.conv_lambert = m_pbr_renderer->environment_lambert();
        pbr_render_info.conv_ggx = m_pbr_renderer->environment_ggx();
        pbr_render_info.settings = m_pbr_renderer->settings;
    }
    m_pbr_renderer = vierkant::PBRDeferred::create(m_device, pbr_render_info);

    vierkant::PBRPathTracer::create_info_t path_tracer_info = {};
    path_tracer_info.num_frames_in_flight = framebuffers.size() + 1;
    path_tracer_info.pipeline_cache = m_pipeline_cache;

    path_tracer_info.settings = m_path_tracer ? m_path_tracer->settings : m_settings.path_tracer_settings;
    path_tracer_info.queue = m_queue_path_tracer;

    m_path_tracer = vierkant::PBRPathTracer::create(m_device, path_tracer_info);

    if(use_raytracer){ m_scene_renderer = m_path_tracer; }
    else{ m_scene_renderer = m_pbr_renderer; }
}

void PBRViewer::create_texture_image()
{
    // try to fetch cool image
    auto http_response = crocore::net::http::get(g_texture_url);

    crocore::ImagePtr img;
    vk::Image::Format fmt;

    // create from downloaded data
    if(!http_response.data.empty()){ img = crocore::create_image_from_data(http_response.data, 3); }
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
    m_textures["test"] = vk::Image::create(m_device, img->data(), fmt);

    // overwrite with a compressed + mipmapped version
    vierkant::bc7::compress_info_t compress_info = {};
    compress_info.image = img;
    compress_info.generate_mipmaps = true;
    compress_info.delegate_fn = [this](const auto &fn){ return background_queue().post(fn); };
    auto compress_result = vierkant::bc7::compress(compress_info);

    LOG_DEBUG << crocore::format("BC7-compressed image (%dx%d, %d mips) in %d ms", img->width(), img->height(),
                                 compress_result.levels.size(), compress_result.duration.count());

    m_textures["test"] = vierkant::model::create_compressed_texture(m_device, compress_result, fmt, m_device->queue());

    if(m_font)
    {
        // draw_gui some text into a texture
        m_textures["font"] = m_font->create_texture(m_device, "Pooop!\nKleines kaka,\ngrosses KAKA ...");
    }
}

void PBRViewer::load_model(const std::string &path)
{
    vierkant::MeshPtr mesh;

    // additionally required buffer-flags for raytracing
    VkBufferUsageFlags buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if(!path.empty())
    {
        m_settings.model_path = path;

        auto load_task = [this, path, buffer_flags]()
        {
            m_num_loading++;
            auto start_time = std::chrono::steady_clock::now();

            // tinygltf
            auto mesh_assets = vierkant::model::gltf(path);

            // assimp
            //auto old_mesh_assets = vierkant::model::load_model(model_path, background_pool);

            if(mesh_assets.entry_create_infos.empty())
            {
                LOG_WARNING << "could not load file: " << path;
                return;
            }

            auto mesh = load_mesh(m_device, mesh_assets, m_settings.texture_compression, m_queue_loading, buffer_flags);

            if(!mesh)
            {
                LOG_WARNING << crocore::format("loading '%s' failed ...", path.c_str());
                m_num_loading--;
                return;
            }

            auto done_cb = [this, mesh, start_time, path]()
            {
                m_selected_objects.clear();
                m_scene->clear();

                auto mesh_node = vierkant::MeshNode::create(mesh);

                // scale
                float scale = 5.f / glm::length(mesh_node->aabb().half_extents());
                mesh_node->set_scale(scale);

                // center aabb
                auto aabb = mesh_node->aabb().transform(mesh_node->transform());
                mesh_node->set_position(-aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 0.f));

                m_scene->add_object(mesh_node);

                if(m_path_tracer){ m_path_tracer->reset_accumulator(); }

                auto dur = double_second(
                        std::chrono::steady_clock::now() - start_time);
                LOG_DEBUG
                << crocore::format("loaded '%s' -- (%.2fs)", path.c_str(),
                                   dur.count());
                m_num_loading--;
            };
            main_queue().post(done_cb);
        };
        background_queue().post(load_task);
    }
    else
    {
        vierkant::Mesh::create_info_t mesh_create_info = {};
        mesh_create_info.buffer_usage_flags = buffer_flags;
        mesh = vk::Mesh::create_from_geometry(m_device, vk::Geometry::Box(glm::vec3(.5f)), mesh_create_info);
        auto mat = vk::Material::create();

        auto it = m_textures.find("test");
        if(it != m_textures.end()){ mat->textures[vierkant::Material::Color] = it->second; }
        mesh->materials = {mat};

        auto mesh_node = vierkant::MeshNode::create(mesh);

        m_selected_objects.clear();
        m_scene->clear();
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
            bool use_float = (img->num_bytes() /
                              (img->width() * img->height() * img->num_components())) > 1;

            // command pool for background transfer
            auto command_pool = vierkant::create_command_pool(m_device, vierkant::Device::Queue::GRAPHICS,
                                                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

            {
                auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());
                cmd_buf.begin();

                vk::Image::Format fmt = {};
                fmt.extent = {img->width(), img->height(), 1};
                fmt.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                fmt.format = use_float ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
                fmt.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                fmt.initial_cmd_buffer = cmd_buf.handle();
                panorama = vk::Image::create(m_device, nullptr, fmt);

                auto buf = vierkant::Buffer::create(m_device, img->data(), img->num_bytes(),
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

                // copy and layout transition
                panorama->copy_from(buf, cmd_buf.handle());
                panorama->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(m_queue_loading, true);

                // derive sane resolution for cube from panorama-width
                float res = crocore::next_pow_2(std::max(img->width(), img->height()) / 4);
                skybox = vierkant::cubemap_from_panorama(panorama, {res, res}, m_queue_loading, true);
            }

            if(skybox)
            {
                constexpr uint32_t lambert_size = 128;

                conv_lambert = vierkant::create_convolution_lambert(m_device, skybox, lambert_size, m_queue_loading);
                conv_ggx = vierkant::create_convolution_ggx(m_device, skybox, skybox->width(), m_queue_loading);

                auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());
                cmd_buf.begin();

//                skybox->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());
                conv_lambert->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());
                conv_ggx->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(m_queue_loading, true);
            }
        }

        main_queue().post([this, path, skybox, conv_lambert, conv_ggx, start_time]()
                          {
                              m_scene->set_environment(skybox);

                              m_pbr_renderer->set_environment(conv_lambert, conv_ggx);

                              m_path_tracer->reset_accumulator();

                              m_settings.environment_path = path;

                              auto dur = double_second(std::chrono::steady_clock::now() - start_time);
                              LOG_DEBUG << crocore::format("loaded '%s' -- (%.2fs)", path.c_str(), dur.count());
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
        m_num_drawcalls = render_result.num_objects;
        return m_renderer.render(framebuffer);
    };

    auto render_scene_overlays = [this, &framebuffer]() -> VkCommandBuffer
    {
        for(auto &obj : m_selected_objects)
        {
            auto modelview = m_camera->view_matrix() * obj->transform();

            auto mesh_node = std::dynamic_pointer_cast<vierkant::MeshNode>(obj);

            if(m_settings.draw_aabbs)
            {
                m_draw_context.draw_boundingbox(m_renderer_overlay, obj->aabb(), modelview,
                                                m_camera->projection_matrix());

                if(mesh_node)
                {
                    for(const auto &entry : mesh_node->mesh->entries)
                    {
                        m_draw_context.draw_boundingbox(m_renderer_overlay, entry.boundingbox,
                                                        modelview * entry.transform,
                                                        m_camera->projection_matrix());
                    }
                }
            }

            if(m_settings.draw_node_hierarchy)
            {
                vierkant::nodes::node_animation_t animation = {};

                if(mesh_node->mesh->animation_index < mesh_node->mesh->node_animations.size())
                {
                    animation = mesh_node->mesh->node_animations[mesh_node->mesh->animation_index];
                }
                auto node = mesh_node->mesh->root_bone ? mesh_node->mesh->root_bone : mesh_node->mesh->root_node;
                m_draw_context.draw_node_hierarchy(m_renderer_overlay, node, animation, modelview,
                                                   m_camera->projection_matrix());
            }
        }
        return m_renderer_overlay.render(framebuffer);
    };

    auto render_gui = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_renderer_gui);

//        if(m_num_loading)
        {
            m_draw_context.draw_text(m_renderer_gui, crocore::to_string(m_num_drawcalls), m_font,
                                     {m_window->size().x - 375, 50.f});
        }
        return m_renderer_gui.render(framebuffer);
    };

    vierkant::window_delegate_t::draw_result_t ret;

    // submit and wait for all command-creation tasks to complete
    std::vector<std::future<VkCommandBuffer>> cmd_futures;
    cmd_futures.push_back(background_queue().post(render_scene));
    cmd_futures.push_back(background_queue().post(render_scene_overlays));
    cmd_futures.push_back(background_queue().post(render_gui));
    crocore::wait_all(cmd_futures);

    // get values from completed futures
    for(auto &f : cmd_futures){ ret.command_buffers.push_back(f.get()); }

    // get semaphore infos
    ret.semaphore_infos = std::move(semaphore_infos);

    return ret;
}

void PBRViewer::save_settings(PBRViewer::settings_t settings, const std::filesystem::path &path) const
{
    vierkant::Window::create_info_t window_info = {};
    window_info.size = m_window->size();
    window_info.position = m_window->position();
    window_info.fullscreen = m_window->fullscreen();
    window_info.sample_count = m_window->swapchain().sample_count();
    window_info.title = m_window->title();

    window_info.vsync = m_window->swapchain().v_sync();

    settings.log_severity = crocore::g_logger.severity();
    settings.window_info = window_info;
    settings.view_rotation = m_camera_control.orbit->rotation;
    settings.view_look_at = m_camera_control.orbit->look_at;
    settings.view_distance = m_camera_control.orbit->distance;
    settings.pbr_settings = m_pbr_renderer->settings;
    settings.path_tracer_settings = m_path_tracer->settings;
    settings.path_tracing = m_scene_renderer == m_path_tracer;

    // create and open a character archive for output
    std::ofstream ofs(path.string());

    // save data to archive
    try
    {
        cereal::JSONOutputArchive archive(ofs);

        // write class instance to archive
        archive(settings);

    } catch(std::exception &e){ LOG_ERROR << e.what(); }

    LOG_DEBUG << "save settings: " << path;
}

PBRViewer::settings_t PBRViewer::load_settings(const std::filesystem::path &path)
{
    PBRViewer::settings_t settings = {};

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
        } catch(std::exception &e){ LOG_ERROR << e.what(); }

        LOG_DEBUG << "loading settings: " << path;
    }
    return settings;
}

void PBRViewer::create_camera_controls()
{
    // init arcball
    m_camera_control.orbit->screen_size = m_window->size();
    m_camera_control.orbit->enabled = true;

    // restore settings
    m_camera_control.orbit->rotation = m_settings.view_rotation;
    m_camera_control.orbit->look_at = m_settings.view_look_at;
    m_camera_control.orbit->distance = m_settings.view_distance;

//    m_camera_control.fly->move_speed =
//m_camera_control.fly->position =
//m_camera_control.fly->rotation =

    // attach arcball mouse delegate
    auto arcball_delegeate = m_camera_control.orbit->mouse_delegate();
    arcball_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.orbit;
        return is_active && !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse);
    };
    m_window->mouse_delegates["arcball"] = std::move(arcball_delegeate);

    auto flycamera_delegeate = m_camera_control.fly->mouse_delegate();
    flycamera_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        return is_active && !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse);
    };
    m_window->mouse_delegates["flycamera"] = std::move(flycamera_delegeate);

    auto fly_key_delegeate = m_camera_control.fly->key_delegate();
    fly_key_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        return is_active && !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureKeyboard);
    };
    m_window->key_delegates["flycamera"] = std::move(fly_key_delegeate);

    // update camera with arcball
    auto transform_cb = [this](const glm::mat4 &transform)
    {
        m_camera->set_global_transform(transform);
        if(m_path_tracer){ m_path_tracer->reset_accumulator(); }
    };
    m_camera_control.orbit->transform_cb = transform_cb;
    m_camera_control.fly->transform_cb = transform_cb;
    m_camera->set_global_transform(m_camera_control.orbit->transform());
}
