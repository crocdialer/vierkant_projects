#include <crocore/filesystem.hpp>
#include <crocore/http.hpp>
#include <crocore/Image.hpp>
#include <vierkant/imgui/imgui_util.h>
#include <vierkant/assimp.hpp>
#include <vierkant/Visitor.hpp>

#include "simple_test.hpp"

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

void render_scene(vierkant::Renderer &renderer, vierkant::ScenePtr scene, vierkant::CameraPtr camera)
{
    // proof-of API, keep it simple here ...
    vierkant::SelectVisitor<vierkant::Mesh> select_visitor;
    scene->root()->accept(select_visitor);

}

void Vierkant3DViewer::setup()
{
    crocore::g_logger.set_severity(crocore::Severity::DEBUG);

    create_context_and_window();
    create_texture_image();
    load_model();
    create_graphics_pipeline();
    create_offscreen_assets();
}

void Vierkant3DViewer::teardown()
{
    LOG_INFO << "ciao " << name();
    vkDeviceWaitIdle(m_device->handle());
}

void Vierkant3DViewer::poll_events()
{
    glfwPollEvents();
}

void Vierkant3DViewer::create_context_and_window()
{
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::get_required_extensions());
    m_window = vk::Window::create(m_instance.handle(), WIDTH, HEIGHT, name(), m_fullscreen);
    m_device = vk::Device::create(m_instance.physical_devices().front(), m_instance.use_validation_layers(),
                                  m_window->surface());
    m_window->create_swapchain(m_device, m_use_msaa ? m_device->max_usable_samples() : VK_SAMPLE_COUNT_1_BIT, V_SYNC);

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
                    m_draw_grid = !m_draw_grid;
                    break;
                case vk::Key::_B:
                    m_draw_aabb = !m_draw_aabb;
                    break;
                default:
                    break;
            }
        }
    };
    m_window->key_delegates[name()] = key_delegate;

    // create a draw context
    m_draw_context = vierkant::DrawContext(m_device);

    // create a gui and add a draw-delegate
    m_gui_context = vk::gui::Context(m_device, g_font_path, 23.f);
    m_gui_context.delegates["application"] = [this]
    {
        vk::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
    };

    // textures window
    m_gui_context.delegates["textures"] = [this]
    {
        std::vector<vierkant::ImagePtr> images;
        for(const auto &pair : m_textures){ images.push_back(pair.second); }
        vk::gui::draw_images_ui(images);
    };

    // animations window
    m_gui_context.delegates["animation"] = [this]
    {
        if(m_mesh && !m_mesh->node_animations.empty())
        {
            auto &animation = m_mesh->node_animations[m_mesh->animation_index];

            ImGui::Begin("animation");

            // animation index
            int animation_index = m_mesh->animation_index;
            if(ImGui::SliderInt("index", &animation_index, 0, m_mesh->node_animations.size() - 1))
            {
                m_mesh->animation_index = animation_index;
            }

            // animation speed
            if(ImGui::SliderFloat("speed", &m_mesh->animation_speed, -3.f, 3.f)){}
            ImGui::SameLine();
            if(ImGui::Checkbox("play", &animation.playing)){}

            float current_time = animation.current_time / animation.ticks_per_sec;
            float duration = animation.duration / animation.ticks_per_sec;

            // animation current time / max time
            if(ImGui::SliderFloat(("/ " + crocore::to_string(duration, 2) + " s").c_str(),
                                  &current_time, 0.f, duration))
            {
                animation.current_time = current_time * animation.ticks_per_sec;
            }

            ImGui::Separator();
            ImGui::End();
        }
    };

    // imgui demo window
    m_gui_context.delegates["demo"] = []{ if(DEMO_GUI){ ImGui::ShowDemoWindow(&DEMO_GUI); }};

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    m_camera = vk::PerspectiveCamera::create(m_window->aspect_ratio(), 45.f, .1f, 100.f);
//    m_camera->set_position(glm::vec3(0.f, 0.f, 4.0f));

    // create arcball
    m_arcball = vk::Arcball(m_window->size());
    m_arcball.multiplier *= -1.f;

    // attach arcball mouse delegate
    m_window->mouse_delegates["arcball"] = m_arcball.mouse_delegate();
    m_window->mouse_delegates["zoom"].mouse_wheel = [this](const vierkant::MouseEvent &e)
    {
        if(!(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse))
        {
            m_cam_distance = std::max(.1f, m_cam_distance - e.wheel_increment().y);
        }
    };

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

    m_font = vk::Font::create(m_device, g_font_path, 64);

    m_pipeline_cache = vk::PipelineCache::create(m_device);
}

void Vierkant3DViewer::create_graphics_pipeline()
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
    create_info.renderpass = m_window->swapchain().framebuffers().front().renderpass();

    m_renderer = vk::Renderer(m_device, create_info);
    m_renderer_gui = vk::Renderer(m_device, create_info);
}

void Vierkant3DViewer::create_offscreen_assets()
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
        frambuffer.clear_color = {0.f, 0.f, 0.f, 0.f};
        renderpass = frambuffer.renderpass();
    }

    vierkant::Renderer::create_info_t create_info = {};
    create_info.num_frames_in_flight = m_window->swapchain().images().size();
    create_info.sample_count = fb_info.color_attachment_format.sample_count;
    create_info.viewport.width = size.x;
    create_info.viewport.height = size.y;
    create_info.viewport.maxDepth = 1;
    create_info.pipeline_cache = m_pipeline_cache;
    create_info.renderpass = renderpass;
    m_renderer_offscreen = vierkant::Renderer(m_device, create_info);
}

void Vierkant3DViewer::create_texture_image()
{
    // try to fetch cool image
    auto http_resonse = cc::net::http::get(g_texture_url);

    crocore::ImagePtr img;
    vk::Image::Format fmt;

    // create from downloaded data
    if(!http_resonse.data.empty()){ img = cc::create_image_from_data(http_resonse.data, 4); }
    else
    {
        // create 2x2 black/white checkerboard image
        uint32_t v[4] = {0xFFFFFFFF, 0xFF000000, 0xFF000000, 0xFFFFFFFF};
        img = cc::Image_<uint8_t>::create(reinterpret_cast<uint8_t *>(v), 2, 2, 4);
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

void Vierkant3DViewer::load_model(const std::string &path)
{
    if(!path.empty())
    {
        auto loading_task = [this, path]()
        {
            auto mesh_assets = vierkant::assimp::load_model(path);
            auto mesh = vk::Mesh::create_from_geometries(m_device, mesh_assets.geometries, mesh_assets.transforms,
                                                         mesh_assets.node_indices);

            // skin + bones
            mesh->root_bone = mesh_assets.root_bone;

            // node hierarchy
            mesh->root_node = mesh_assets.root_node;

            // node animations
            mesh->node_animations = std::move(mesh_assets.node_animations);

            mesh->materials.resize(mesh->entries.size());
            std::vector<vierkant::MaterialPtr> materials_tmp(mesh_assets.materials.size());

            for(uint32_t i = 0; i < materials_tmp.size(); ++i)
            {
                auto &material = materials_tmp[i];
                material = vierkant::Material::create();

                material->color = mesh_assets.materials[i].diffuse;
                material->emission = mesh_assets.materials[i].emission;
                material->roughness = mesh_assets.materials[i].roughness;
                material->blending = mesh_assets.materials[i].blending;

                auto color_img = mesh_assets.materials[i].img_diffuse;

                if(color_img)
                {
                    vk::Image::Format fmt;
                    fmt.format = vk_format(color_img);
                    fmt.extent = {color_img->width(), color_img->height(), 1};
                    fmt.use_mipmap = true;
                    fmt.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    fmt.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
                    auto color_tex = vk::Image::create(m_device, color_img->data(), fmt);
                    material->shader_type = mesh->root_bone ? vk::ShaderType::UNLIT_TEXTURE_SKIN
                                                            : vk::ShaderType::UNLIT_TEXTURE;
                    material->images = {color_tex};
                }
                else
                {
                    material->shader_type = mesh->root_bone ? vk::ShaderType::UNLIT_COLOR_SKIN
                                                            : vk::ShaderType::UNLIT_COLOR;
                    material->images = {};
                }
            }

            // correct material indices
            for(uint32_t i = 0; i < mesh->entries.size(); ++i)
            {
                mesh->materials[i] = materials_tmp[mesh_assets.material_indices[i]];
            }

            // scale
            float scale = 5.f / glm::length(mesh->aabb().halfExtents());
            mesh->set_scale(scale);

            // center aabb
            auto aabb = mesh->aabb().transform(mesh->transform());
            mesh->set_position(-aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 0.f));

            m_mesh = mesh;
//            main_queue().post([this, mesh](){ m_mesh = mesh; });
        };

        loading_task();
//        background_queue().post(loading_task);
    }
    else
    {
        m_mesh = vk::Mesh::create_from_geometries(m_device, {vk::Geometry::Box(glm::vec3(.5f))});
        auto mat = vk::Material::create();
        mat->shader_type = vk::ShaderType::UNLIT_TEXTURE;

        auto it = m_textures.find("test");
        if(it != m_textures.end()){ mat->images = {it->second}; }
        m_mesh->materials = {mat};
    }
}

void Vierkant3DViewer::load_environment(const std::string &path)
{
    auto img = crocore::create_image_from_file(path, 4);

    if(img)
    {
        bool use_float = (img->num_bytes() / (img->width() * img->height() * img->num_components())) > 1;

        vk::Image::Format fmt = {};
        fmt.extent = {img->width(), img->height(), 1};
        fmt.format = use_float ? VK_FORMAT_R32G32B32A32_SFLOAT : VK_FORMAT_R8G8B8A8_UNORM;
        auto tex = vk::Image::create(m_device, img->data(), fmt);

        // tmp
        m_textures["environment"] = tex;

        auto cubemap = vierkant::cubemap_from_panorama(tex, {1024, 1024});

        if(!m_skybox)
        {
            auto box = vierkant::Geometry::Box();
            m_skybox = vierkant::Mesh::create_from_geometries(m_device, {box});
            auto &mat = m_skybox->materials.front();
            mat->shader_type = vierkant::ShaderType::UNLIT_CUBE;
            mat->depth_write = false;
            mat->depth_test = true;
            mat->cull_mode = VK_CULL_MODE_FRONT_BIT;
        }
        for(auto &mat : m_skybox->materials){ mat->images = {cubemap}; }
    }
}

vierkant::ImagePtr Vierkant3DViewer::render_offscreen(vierkant::Framebuffer &framebuffer,
                                                      vierkant::Renderer &renderer,
                                                      const std::function<void()> &functor,
                                                      VkQueue queue,
                                                      bool sync)
{
    // wait for prior frame to finish
    framebuffer.wait_fence();

    VkCommandBufferInheritanceInfo inheritance = {};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.framebuffer = framebuffer.handle();
    inheritance.renderPass = framebuffer.renderpass().get();

    // invoke function-object to stage drawables
    functor();

    // create a commandbuffer
    VkCommandBuffer cmd_buffer = renderer.render(&inheritance);

    // submit rendering commands to queue
    auto fence = framebuffer.submit({cmd_buffer}, queue ? queue : renderer.device()->queue());

    if(sync){ vkWaitForFences(renderer.device()->handle(), 1, &fence, VK_TRUE, std::numeric_limits<uint64_t>::max()); }

    // check for resolve-attachment, fallback to color-attachment
    auto attach_it = framebuffer.attachments().find(vierkant::Framebuffer::AttachmentType::Resolve);

    if(attach_it == framebuffer.attachments().end())
    {
        attach_it = framebuffer.attachments().find(vierkant::Framebuffer::AttachmentType::Color);
    }

    // return color-attachment
    if(attach_it != framebuffer.attachments().end()){ return attach_it->second.front(); }
    return nullptr;
}

void Vierkant3DViewer::update(double time_delta)
{
    m_arcball.enabled = !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse);

    auto look_at = glm::vec3(0);

    glm::mat4 rotation = glm::mat4_cast(m_arcball.rotation());

    auto tmp = glm::translate(glm::mat4(1), look_at + glm::vec3(0, 0, m_cam_distance));
    auto cam_transform = rotation * tmp;
    m_camera->set_global_transform(cam_transform);

//    m_camera_handle->set_rotation(m_arcball.rotation());
//    m_arcball.update(time_delta);

    if(m_mesh && m_mesh->animation_index < m_mesh->node_animations.size())
    {
        // update node animation
        vierkant::update_animation(m_mesh->node_animations[m_mesh->animation_index],
                                   static_cast<float>(time_delta),
                                   m_mesh->animation_speed);

        // entry animation transforms
        std::vector<glm::mat4> node_matrices;
        vierkant::nodes::build_node_matrices(m_mesh->root_node,
                                             m_mesh->node_animations[m_mesh->animation_index],
                                             node_matrices);

        for(auto &entry : m_mesh->entries){ entry.transform = node_matrices[entry.node_index]; }
    }

    auto image_index = m_window->swapchain().image_index();
    auto &framebuffer = m_framebuffers_offscreen[image_index];

    m_textures["offscreen"] = render_offscreen(framebuffer, m_renderer_offscreen, [this, &framebuffer]()
    {
        auto projection = glm::perspectiveRH(m_camera->fov(),
                                             framebuffer.extent().width / (float) framebuffer.extent().width,
                                             m_camera->near(), m_camera->far());
        projection[1][1] *= -1;

        m_draw_context.draw_mesh(m_renderer_offscreen, m_mesh, m_camera->view_matrix(), projection);
    });

//    if(m_textures["environment"]){ m_cubemap = vierkant::cubemap_from_panorama(m_textures["environment"]); }

    // issue top-level draw-command
    m_window->draw();
}

std::vector<VkCommandBuffer> Vierkant3DViewer::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = m_window->swapchain().framebuffers()[image_index];

    VkCommandBufferInheritanceInfo inheritance = {};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.framebuffer = framebuffer.handle();
    inheritance.renderPass = framebuffer.renderpass().get();

    auto render_mesh = [this, &inheritance]() -> VkCommandBuffer
    {
        m_draw_context.draw_mesh(m_renderer, m_mesh, m_camera->view_matrix(), m_camera->projection_matrix());

        glm::mat4 m = m_camera->view_matrix();
        m[3] = glm::vec4(0, 0, 0, 1);
        m = glm::scale(m, glm::vec3(m_camera->far() * .99f));
        m_draw_context.draw_mesh(m_renderer, m_skybox, m, m_camera->projection_matrix());

        if(m_draw_aabb)
        {
            for(const auto &entry : m_mesh->entries)
            {
                m_draw_context.draw_boundingbox(m_renderer, entry.boundingbox,
                                                m_camera->view_matrix() * m_mesh->transform() * entry.transform,
                                                m_camera->projection_matrix());
            }
//            m_draw_context.draw_boundingbox(m_renderer, m_mesh->aabb(),
//                                            m_camera->view_matrix() * m_mesh->transform(),
//                                            m_camera->projection_matrix());
        }
        if(m_draw_grid)
        {
            m_draw_context.draw_grid(m_renderer, 10.f, 100, m_camera->view_matrix(), m_camera->projection_matrix());
        }
        return m_renderer.render(&inheritance);
    };

    auto render_gui = [this, &inheritance]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_renderer_gui);
//        m_draw_context.draw_text(m_gui_renderer, "$$$ oder fahrkarte du nase\nteil zwo", m_font, {400.f, 450.f});
        return m_renderer_gui.render(&inheritance);
    };

    // submit and wait for all command-creation tasks to complete
    std::vector<std::future<VkCommandBuffer>> cmd_futures;
    cmd_futures.push_back(background_queue().post(render_mesh));
    cmd_futures.push_back(background_queue().post(render_gui));
    crocore::wait_all(cmd_futures);

    // get values from completed futures
    std::vector<VkCommandBuffer> command_buffers;
    for(auto &f : cmd_futures){ command_buffers.push_back(f.get()); }
    return command_buffers;
}
