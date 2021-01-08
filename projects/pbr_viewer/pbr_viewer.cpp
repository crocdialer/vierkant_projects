#include <fstream>

#include <crocore/filesystem.hpp>
#include <crocore/http.hpp>
#include <crocore/Image.hpp>
#include <crocore/json.hpp>

#include <vierkant/imgui/imgui_util.h>
#include <vierkant/assimp.hpp>
#include <vierkant/Visitor.hpp>
#include <vierkant/UnlitForward.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/cubemap_utils.hpp>

#include "pbr_viewer.hpp"

////////////////////////////// VALIDATION LAYER ///////////////////////////////////////////////////

#ifdef NDEBUG
const bool g_enable_validation_layers = false;
#else
const bool g_enable_validation_layers = true;
#endif

using double_second = std::chrono::duration<double>;

VkFormat vk_format(const crocore::ImagePtr &img, bool compress)
{
    VkFormat ret = VK_FORMAT_UNDEFINED;

    switch(img->num_components())
    {
        case 1:
            ret = VK_FORMAT_R8_UNORM;
            break;
        case 2:
            ret = VK_FORMAT_R8G8_UNORM;
            break;
        case 3:
            ret = VK_FORMAT_R8G8B8_UNORM;
            break;
        case 4:
            ret = VK_FORMAT_R8G8B8A8_UNORM;
            break;
    }
    return ret;
}

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
    create_offscreen_assets();

    // load stuff
    load_model(m_settings.model_path);
    load_environment(m_settings.environment_path);
}

void PBRViewer::teardown()
{
    LOG_INFO << "ciao " << name();
    vkDeviceWaitIdle(m_device->handle());
}

void PBRViewer::poll_events()
{
    glfwPollEvents();
}

void PBRViewer::create_context_and_window()
{
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::get_required_extensions());

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

        m_arcball.screen_size = {w, h};
    };
    window_delegate.close_fn = [this](){ set_running(false); };
    m_window->window_delegates[name()] = window_delegate;

    // create a draw context
    m_draw_context = vierkant::DrawContext(m_device);

    m_font = vk::Font::create(m_device, g_font_path, 64);

    m_pipeline_cache = vk::PipelineCache::create(m_device);
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
                case vk::Key::_ESCAPE:
                    set_running(false);
                    break;
                case vk::Key::_G:
                    m_pbr_renderer->settings.draw_grid = !m_pbr_renderer->settings.draw_grid;
                    break;
                case vk::Key::_B:
                    m_settings.draw_aabbs = !m_settings.draw_aabbs;
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

    // create a gui and add a draw-delegate
    m_gui_context = vk::gui::Context(m_device, g_font_path, 23.f);
    m_gui_context.delegates["application"] = [this]
    {
        vk::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
    };

    // renderer window
    m_gui_context.delegates["renderer"] = [this]{ vk::gui::draw_scene_renderer_ui(m_pbr_renderer, m_camera); };

    // scenegraph window
    m_gui_context.delegates["scenegraph"] = [this]{ vk::gui::draw_scene_ui(m_scene, m_camera, &m_selected_objects); };

    // imgui demo window
    m_gui_context.delegates["demo"] = []{ if(DEMO_GUI){ ImGui::ShowDemoWindow(&DEMO_GUI); }};

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    m_camera = vk::PerspectiveCamera::create(m_window->aspect_ratio(), 45.f, .1f, 100.f);

    // create arcball
    m_arcball = vk::Arcball(m_window->size());

    m_arcball.rotation = m_settings.view_rotation;
    m_arcball.look_at = m_settings.view_look_at;
    m_arcball.distance = m_settings.view_distance;

    // attach arcball mouse delegate
    auto arcball_delegeate = m_arcball.mouse_delegate();
    arcball_delegeate.enabled = [this]()
    {
        return !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse);
    };
    m_window->mouse_delegates["arcball"] = std::move(arcball_delegeate);

    vierkant::mouse_delegate_t simple_mouse = {};
    simple_mouse.mouse_wheel = [this](const vierkant::MouseEvent &e)
    {
        if(!(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse))
        {
            m_arcball.distance = std::max(.1f, m_arcball.distance - e.wheel_increment().y);
        }
    };
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
    m_renderer_gui = vk::Renderer(m_device, create_info);

    vierkant::PBRDeferred::create_info_t pbr_render_info = {};
    pbr_render_info.num_frames_in_flight = framebuffers.size();
    pbr_render_info.size = fb_extent;
    pbr_render_info.pipeline_cache = m_pipeline_cache;
    pbr_render_info.settings = m_settings.render_settings;

    if(m_pbr_renderer)
    {
        pbr_render_info.conv_lambert = m_pbr_renderer->environment_lambert();
        pbr_render_info.conv_ggx = m_pbr_renderer->environment_ggx();
        pbr_render_info.settings = m_pbr_renderer->settings;
    }
    m_pbr_renderer = vierkant::PBRDeferred::create(m_device, pbr_render_info);
}

void PBRViewer::create_offscreen_assets()
{
    glm::uvec2 size(1024, 1024);

    m_framebuffers_offscreen.resize(m_window->swapchain().images().size());

    vierkant::Framebuffer::create_info_t fb_info = {};
    fb_info.color_attachment_format.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    fb_info.size = {size.x, size.y, 1};
    fb_info.depth = true;

    vierkant::RenderPassPtr renderpass;

    for(auto &frambuffer : m_framebuffers_offscreen)
    {
        frambuffer = vierkant::Framebuffer(m_device, fb_info, renderpass);
        frambuffer.clear_color = {{0.f, 0.f, 0.f, 0.f}};
        renderpass = frambuffer.renderpass();
    }

    vierkant::Renderer::create_info_t create_info = {};
    create_info.num_frames_in_flight = m_window->swapchain().images().size();
    create_info.sample_count = fb_info.color_attachment_format.sample_count;
    create_info.viewport.width = size.x;
    create_info.viewport.height = size.y;
    create_info.viewport.maxDepth = 1;
    create_info.pipeline_cache = m_pipeline_cache;
    m_renderer_offscreen = vierkant::Renderer(m_device, create_info);
    m_unlit_renderer = vierkant::UnlitForward::create(m_device);
}

void PBRViewer::create_texture_image()
{
    // try to fetch cool image
    auto http_resonse = crocore::net::http::get(g_texture_url);

    crocore::ImagePtr img;
    vk::Image::Format fmt;

    // create from downloaded data
    if(!http_resonse.data.empty()){ img = crocore::create_image_from_data(http_resonse.data, 4); }
    else
    {
        // create 2x2 black/white checkerboard image
        uint32_t v[4] = {0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF};
        img = crocore::Image_<uint8_t>::create(reinterpret_cast<uint8_t *>(v), 2, 2, 4);
        fmt.mag_filter = VK_FILTER_NEAREST;
        fmt.format = VK_FORMAT_R8G8B8A8_UNORM;
    }
    fmt.extent = {img->width(), img->height(), 1};
    fmt.use_mipmap = true;
    m_textures["test"] = vk::Image::create(m_device, img->data(), fmt);

    if(m_font)
    {
        // draw_gui some text into a texture
        m_textures["font"] = m_font->create_texture(m_device, "Pooop!\nKleines kaka,\ngrosses KAKA ...");
    }
}

void PBRViewer::load_model(const std::string &path)
{
    vierkant::MeshPtr mesh;

    if(!path.empty())
    {
        m_settings.model_path = path;

        auto load_mesh = [this, path]() -> vierkant::MeshPtr
        {
            auto mesh_assets = vierkant::assimp::load_model(path, background_queue());
            auto mesh = vk::Mesh::create_with_entries(m_device, mesh_assets.entry_create_infos);

            if(!mesh)
            {
                LOG_WARNING << "could not load mesh: " << path;
                return nullptr;
            }

            std::vector<vierkant::BufferPtr> staging_buffers;

            VkQueue queue = m_device->queues(vierkant::Device::Queue::GRAPHICS)[1];

            // command pool for background transfer
            auto command_pool = vierkant::create_command_pool(m_device, vierkant::Device::Queue::GRAPHICS,
                                                              VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

            auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());

            auto create_texture = [device = m_device, cmd_buf_handle = cmd_buf.handle(), &staging_buffers](
                    const crocore::ImagePtr &img) -> vierkant::ImagePtr
            {
                vk::Image::Format fmt;
                fmt.format = vk_format(img);
                fmt.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                fmt.extent = {img->width(), img->height(), 1};
                fmt.use_mipmap = true;
                fmt.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                fmt.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                fmt.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                fmt.initial_cmd_buffer = cmd_buf_handle;

                auto vk_img = vk::Image::create(device, nullptr, fmt);
                auto buf = vierkant::Buffer::create(device, img->data(), img->num_bytes(),
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
                vk_img->copy_from(buf, cmd_buf_handle);
                staging_buffers.push_back(std::move(buf));
                return vk_img;
            };

            cmd_buf.begin();

            // skin + bones
            mesh->root_bone = mesh_assets.root_bone;

            // node hierarchy
            mesh->root_node = mesh_assets.root_node;

            // node animations
            mesh->node_animations = std::move(mesh_assets.node_animations);

            mesh->materials.resize(mesh_assets.materials.size());

            for(uint32_t i = 0; i < mesh->materials.size(); ++i)
            {
                auto &material = mesh->materials[i];
                material = vierkant::Material::create();

                material->color = mesh_assets.materials[i].diffuse;
                material->emission = mesh_assets.materials[i].emission;
                material->roughness = mesh_assets.materials[i].roughness;
                material->blending = mesh_assets.materials[i].blending;

                auto color_img = mesh_assets.materials[i].img_diffuse;
                auto emmission_img = mesh_assets.materials[i].img_emission;
                auto normal_img = mesh_assets.materials[i].img_normals;
                auto ao_rough_metal_img = mesh_assets.materials[i].img_ao_roughness_metal;

                if(color_img){ material->textures[vierkant::Material::Color] = create_texture(color_img); }
                if(emmission_img){ material->textures[vierkant::Material::Emission] = create_texture(emmission_img); }
                if(normal_img){ material->textures[vierkant::Material::Normal] = create_texture(normal_img); }

                if(ao_rough_metal_img)
                {
                    material->textures[vierkant::Material::Ao_rough_metal] = create_texture(ao_rough_metal_img);
                }
            }

            // submit transfer and sync
            cmd_buf.submit(queue, true);

            // scale
            float scale = 5.f / glm::length(mesh->aabb().half_extents());
            mesh->set_scale(scale);

            // center aabb
            auto aabb = mesh->aabb().transform(mesh->transform());
            mesh->set_position(-aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 0.f));

            return mesh;
        };

        background_queue().post([this, load_mesh, path]()
                                {
                                    m_num_loading++;
                                    auto start_time = std::chrono::steady_clock::now();

                                    auto mesh = load_mesh();
                                    main_queue().post([this, mesh, start_time, path]()
                                                      {
                                                          m_selected_objects.clear();
                                                          m_scene->clear();
                                                          m_scene->add_object(mesh);

                                                          auto dur = double_second(
                                                                  std::chrono::steady_clock::now() - start_time);
                                                          LOG_DEBUG
                                                          << crocore::format("loaded '%s' -- (%.2fs)", path.c_str(),
                                                                             dur.count());
                                                          m_num_loading--;
                                                      });
                                });
    }
    else
    {
        mesh = vk::Mesh::create_from_geometry(m_device, vk::Geometry::Box(glm::vec3(.5f)));
        auto mat = vk::Material::create();

        auto it = m_textures.find("test");
        if(it != m_textures.end()){ mat->textures[vierkant::Material::Color] = it->second; }
        mesh->materials = {mat};

        m_selected_objects.clear();
        m_scene->clear();
        m_scene->add_object(mesh);
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
            // some "specific" queue lol
            VkQueue queue = m_device->queues(vierkant::Device::Queue::GRAPHICS)[1];

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
                                                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);

                // copy and layout transition
                panorama->copy_from(buf, cmd_buf.handle());
                panorama->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(queue, true);

                // derive sane resolution for cube from panorama-width
                float res = crocore::next_pow_2(std::max(img->width(), img->height()) / 4);
                skybox = vierkant::cubemap_from_panorama(panorama, {res, res}, queue, true);
            }

            if(skybox)
            {
                constexpr uint32_t lambert_size = 128;

                conv_lambert = vierkant::create_convolution_lambert(m_device, skybox, lambert_size, queue);
                conv_ggx = vierkant::create_convolution_ggx(m_device, skybox, skybox->width(), queue);

                auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());
                cmd_buf.begin();

                conv_lambert->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());
                conv_ggx->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, cmd_buf.handle());

                // submit and sync
                cmd_buf.submit(queue, true);
            }
        }

        main_queue().post([this, path, skybox, conv_lambert, conv_ggx, start_time]()
                          {
                              // tmp
//                              m_textures["environment"] = panorama;

                              m_scene->set_enironment(skybox);

                              m_pbr_renderer->set_environment(conv_lambert, conv_ggx);

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
    // update camera with arcball
    m_camera->set_global_transform(m_arcball.transform());

    // update animated objects in the scene
    m_scene->update(time_delta);

//    auto image_index = m_window->swapchain().image_index();
//    auto &framebuffer = m_framebuffers_offscreen[image_index];
//
//    m_textures["offscreen"] = render_offscreen(framebuffer, m_renderer_offscreen, [this, &framebuffer]()
//    {
//        auto cam = vierkant::PerspectiveCamera::create(framebuffer.extent().width / (float) framebuffer.extent().height,
//                                                       m_camera->fov());
//        cam->set_transform(m_camera->transform());
//
//        m_unlit_renderer->render_scene(m_renderer_offscreen, m_scene, cam, {});
//    });

    // issue top-level draw-command
    m_window->draw();
}

std::vector<VkCommandBuffer> PBRViewer::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = m_window->swapchain().framebuffers()[image_index];

    auto render_scene = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_pbr_renderer->render_scene(m_renderer, m_scene, m_camera, {});

        if(m_settings.draw_aabbs)
        {
            for(auto &obj : m_selected_objects)
            {
                m_draw_context.draw_boundingbox(m_renderer, obj->aabb(),
                                                m_camera->view_matrix() * obj->transform(),
                                                m_camera->projection_matrix());

                auto mesh = std::dynamic_pointer_cast<vierkant::Mesh>(obj);

                if(mesh)
                {
                    for(const auto &entry : mesh->entries)
                    {
                        m_draw_context.draw_boundingbox(m_renderer, entry.boundingbox,
                                                        m_camera->view_matrix() * mesh->transform() *
                                                        entry.transform,
                                                        m_camera->projection_matrix());
                    }
                }
            }
        }
        return m_renderer.render(framebuffer);
    };

    auto render_gui = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_renderer_gui);

        if(m_num_loading)
        {
            m_draw_context.draw_text(m_renderer_gui, "loading ...", m_font, {m_window->size().x - 375, 50.f});
        }
        return m_renderer_gui.render(framebuffer);
    };

    // submit and wait for all command-creation tasks to complete
    std::vector<std::future<VkCommandBuffer>> cmd_futures;
    cmd_futures.push_back(background_queue().post(render_scene));
    cmd_futures.push_back(background_queue().post(render_gui));
    crocore::wait_all(cmd_futures);

    // get values from completed futures
    std::vector<VkCommandBuffer> command_buffers;
    for(auto &f : cmd_futures){ command_buffers.push_back(f.get()); }
    return command_buffers;
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
    settings.view_rotation = m_arcball.rotation;
    settings.view_look_at = m_arcball.look_at;
    settings.view_distance = m_arcball.distance;
    settings.render_settings = m_pbr_renderer->settings;

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
