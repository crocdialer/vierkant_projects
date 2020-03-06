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

void HelloTriangleApplication::setup()
{
    crocore::g_logger.set_severity(crocore::Severity::DEBUG);

    create_context_and_window();
    create_texture_image();
    load_model();
    create_graphics_pipeline();
}

void HelloTriangleApplication::teardown()
{
    LOG_INFO << "ciao " << name();
    vkDeviceWaitIdle(m_device->handle());
}

void HelloTriangleApplication::poll_events()
{
    glfwPollEvents();
}

void HelloTriangleApplication::create_context_and_window()
{
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::get_required_extensions());
    m_window = vk::Window::create(m_instance.handle(), WIDTH, HEIGHT, name(), m_fullscreen);
    m_device = vk::Device::create(m_instance.physical_devices().front(), m_instance.use_validation_layers(),
                                  m_window->surface());
    m_window->create_swapchain(m_device, m_use_msaa ? m_device->max_usable_samples() : VK_SAMPLE_COUNT_1_BIT, V_SYNC);

    // create a WindowDelegate
    vierkant::window_delegate_t window_delegate = {};
    window_delegate.draw_fn = std::bind(&HelloTriangleApplication::draw, this, std::placeholders::_1);
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
    m_gui_context.delegates["textures"] = [this]{ vk::gui::draw_images_ui({m_texture, m_texture_font}); };

    // animations window
    m_gui_context.delegates["animation"] = [this]
    {
        if(m_mesh && m_mesh->root_bone)
        {
            auto &animation = m_mesh->bone_animations[m_mesh->bone_animation_index];

            ImGui::Begin("animation");

            // animation index
            int animation_index = m_mesh->bone_animation_index;
            if(ImGui::SliderInt("index", &animation_index, 0, m_mesh->bone_animations.size() - 1))
            {
                m_mesh->bone_animation_index = animation_index;
            }

            // animation speed
            if(ImGui::SliderFloat("speed", &m_mesh->bone_animation_speed, -3.f, 3.f))
            {
            }

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
        m_cam_distance = std::max(.1f, m_cam_distance - e.wheel_increment().y);
    };

    // attach drag/drop mouse-delegate
    vierkant::mouse_delegate_t file_drop_delegate = {};
    file_drop_delegate.file_drop = [this](const vierkant::MouseEvent &e, const std::vector<std::string> &files)
    {
        load_model(files.back());
    };
    m_window->mouse_delegates["filedrop"] = file_drop_delegate;

    m_animation = crocore::Animation::create(&m_scale, 0.5f, 1.5f, 2.);
    m_animation.set_ease_function(crocore::easing::EaseOutBounce());
    m_animation.set_loop_type(crocore::Animation::LOOP_BACK_FORTH);
    m_animation.start();

    m_animation.set_duration(3.);

    m_font = vk::Font::create(m_device, g_font_path, 64);

    m_pipeline_cache = vk::PipelineCache::create(m_device);
}

void HelloTriangleApplication::create_graphics_pipeline()
{
    m_pipeline_cache->clear();

    auto &framebuffers = m_window->swapchain().framebuffers();
    m_renderer = vk::Renderer(m_device, framebuffers, m_pipeline_cache);
    m_image_renderer = vk::Renderer(m_device, framebuffers, m_pipeline_cache);
    m_gui_renderer = vk::Renderer(m_device, framebuffers, m_pipeline_cache);
}

void HelloTriangleApplication::create_texture_image()
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
    m_texture = vk::Image::create(m_device, img->data(), fmt);

    if(m_font)
    {
        // draw_gui some text into a texture
        m_texture_font = m_font->create_texture(m_device, "Pooop!\nKleines kaka,\ngrosses KAKA ...");
    }
}

void HelloTriangleApplication::load_model(const std::string &path)
{
    m_drawables.clear();

    if(!path.empty())
    {
        auto mesh_assets = vierkant::assimp::load_model(path);
        m_mesh = vk::Mesh::create_from_geometries(m_device, mesh_assets.geometries);

        // skin + bones
        m_mesh->root_bone = mesh_assets.root_bone;
        m_mesh->bone_animations = std::move(mesh_assets.animations);

        m_mesh->materials.resize(m_mesh->entries.size());
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
                material->shader_type = m_mesh->root_bone ? vk::ShaderType::UNLIT_TEXTURE_SKIN
                                                          : vk::ShaderType::UNLIT_TEXTURE;
                material->images = {color_tex};
            }
            else
            {
                material->shader_type = m_mesh->root_bone ? vk::ShaderType::UNLIT_COLOR_SKIN
                                                          : vk::ShaderType::UNLIT_COLOR;
                material->images = {};
            }
        }

        // correct material indices
        for(uint32_t i = 0; i < m_mesh->entries.size(); ++i)
        {
            m_mesh->materials[i] = materials_tmp[mesh_assets.material_indices[i]];
        }

        // scale
        float scale = 5.f / glm::length(m_mesh->aabb().halfExtents());
        m_mesh->set_scale(scale);

        m_drawables = vk::Renderer::create_drawables(m_device, m_mesh, m_mesh->materials);
    }
    else
    {
        m_mesh = vk::Mesh::create_from_geometries(m_device, {vk::Geometry::Box(glm::vec3(.5f))});
        m_material->shader_type = vk::ShaderType::UNLIT_TEXTURE;
        m_material->images = {m_texture};
        m_drawables = vk::Renderer::create_drawables(m_device, m_mesh, {m_material});
    }
}

void HelloTriangleApplication::update(double time_delta)
{
    m_arcball.enabled = !(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureMouse);

    auto look_at = glm::vec3(0);

    glm::mat4 rotation = glm::mat4_cast(m_arcball.rotation());

    auto tmp = glm::translate(glm::mat4(1), look_at + glm::vec3(0, 0, m_cam_distance));
    auto cam_transform = rotation * tmp;
    m_camera->set_global_transform(cam_transform);

//    m_camera_handle->set_rotation(m_arcball.rotation());
//    m_arcball.update(time_delta);

    if(m_mesh && m_mesh->bone_animation_index < m_mesh->bone_animations.size())
    {
        auto &anim = m_mesh->bone_animations[m_mesh->bone_animation_index];
        anim.current_time = fmodf(anim.current_time + time_delta * anim.ticks_per_sec * m_mesh->bone_animation_speed,
                                  anim.duration);
        anim.current_time += anim.current_time < 0.f ? anim.duration : 0.f;
    }

    m_animation.update();

    // update matrices for this frame
//    m_mesh->transform() = glm::rotate(glm::scale(glm::mat4(1), glm::vec3(m_mesh->scale())),
//                                          (float) application_time() * glm::radians(30.0f),
//                                          glm::vec3(0.0f, 1.0f, 0.0f));


    // TODO: creating / updating those will be moved to a CullVisitor
    for(auto &drawable : m_drawables)
    {
        drawable.matrices.model = m_mesh->transform();
        drawable.matrices.view = m_camera->view_matrix();
        drawable.matrices.projection = m_camera->projection_matrix();
    }

    // issue top-level draw-command
    m_window->draw();
}

std::vector<VkCommandBuffer> HelloTriangleApplication::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = m_window->swapchain().framebuffers()[image_index];
    int32_t width = m_window->swapchain().extent().width, height = m_window->swapchain().extent().height;

    VkCommandBufferInheritanceInfo inheritance = {};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritance.framebuffer = framebuffer.handle();
    inheritance.renderPass = framebuffer.renderpass().get();

    auto render_images = [this, width, height, &inheritance]() -> VkCommandBuffer
    {
        m_draw_context.draw_image(m_image_renderer, m_texture);
        m_draw_context.draw_image(m_image_renderer, m_texture, {width / 4, height / 4, width / 2, height / 2});
        m_draw_context.draw_image(m_image_renderer, m_texture_font, {width / 4, height / 4, width / 2, height / 2});
        return m_image_renderer.render(&inheritance);
    };

    auto render_mesh = [this, &inheritance]() -> VkCommandBuffer
    {
        for(auto &drawable : m_drawables){ m_renderer.stage_drawable(drawable); }

        if(m_draw_aabb)
        {
            m_draw_context.draw_boundingbox(m_renderer, m_mesh->aabb(),
                                            m_camera->view_matrix() * m_mesh->transform(),
                                            m_camera->projection_matrix());
        }
        if(m_draw_grid)
        {
            m_draw_context.draw_grid(m_renderer, 10.f, 100, m_camera->view_matrix(), m_camera->projection_matrix());
        }
        return m_renderer.render(&inheritance);
    };

    auto render_gui = [this, &inheritance]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_gui_renderer);
//        m_draw_context.draw_text(m_gui_renderer, "$$$ oder fahrkarte du nase\nteil zwo", m_font, {400.f, 450.f});
        return m_gui_renderer.render(&inheritance);
    };

    bool concurrant_draw = true;

    if(concurrant_draw)
    {
        // submit and wait for all command-creation tasks to complete
        std::vector<std::future<VkCommandBuffer>> cmd_futures;
//        cmd_futures.push_back(background_queue().post(render_images));
        cmd_futures.push_back(background_queue().post(render_mesh));
        cmd_futures.push_back(background_queue().post(render_gui));
        crocore::wait_all(cmd_futures);

        // get values from completed futures
        std::vector<VkCommandBuffer> command_buffers;
        for(auto &f : cmd_futures){ command_buffers.push_back(f.get()); }
        return command_buffers;
    }
    return {render_images(), render_mesh(), render_gui()};
}
