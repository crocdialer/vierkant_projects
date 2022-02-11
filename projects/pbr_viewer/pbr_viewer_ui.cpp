//
// Created by crocdialer on 2/11/22.
//

#include <crocore/http.hpp>
#include <crocore/filesystem.hpp>
#include <vierkant/imgui/imgui_util.h>

#include "pbr_viewer.hpp"

bool DEMO_GUI = true;

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

                case vierkant::Key::_SPACE:
                    m_settings.draw_ui = !m_settings.draw_ui;
                    break;

                case vierkant::Key::_C:
                    if(m_camera_control.current == m_camera_control.orbit)
                    {
                        m_camera_control.current = m_camera_control.fly;
                    }
                    else{ m_camera_control.current = m_camera_control.orbit; }
                    m_camera->set_transform(m_camera_control.current->transform());
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

    vierkant::joystick_delegate_t joystick_delegate = {};
    joystick_delegate.joystick_cb = [&](const auto &joysticks)
    {
        if(!joysticks.empty())
        {
            auto &js = joysticks.front();
            for(auto &[input, event] : js.input_events())
            {
                spdlog::trace("{}: {} {}", js.name(), vierkant::to_string(input),
                              (event == vierkant::Joystick::Event::BUTTON_PRESS ? " pressed" : " released"));

                if(event == vierkant::Joystick::Event::BUTTON_PRESS)
                {
                    switch(input)
                    {
                        case vierkant::Joystick::Input::BUTTON_MENU:
                            m_settings.draw_ui = !m_settings.draw_ui;
                            break;

                        case vierkant::Joystick::Input::BUTTON_Y:
                            if(m_scene_renderer == m_pbr_renderer){ m_scene_renderer = m_path_tracer; }
                            else{ m_scene_renderer = m_pbr_renderer; }
                            break;

                        case vierkant::Joystick::Input::BUTTON_B:
                            m_settings.draw_aabbs = !m_settings.draw_aabbs;
                            break;

                        case vierkant::Joystick::Input::BUTTON_BACK:
                            if(m_camera_control.current == m_camera_control.orbit)
                            {
                                m_camera_control.current = m_camera_control.fly;
                            }
                            else{ m_camera_control.current = m_camera_control.orbit; }
                            m_camera->set_transform(m_camera_control.current->transform());
                            break;

                        default:
                            break;
                    }
                }
            }
        }
    };
    m_window->joystick_delegates[name()] = joystick_delegate;

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

    // log window
    m_gui_context.delegates["logger"] = [&log_queue = m_log_queue]{ vierkant::gui::draw_logger_ui(log_queue); };

    // scenegraph window
    m_gui_context.delegates["scenegraph"] = [this]{ vk::gui::draw_scene_ui(m_scene, m_camera, &m_selected_objects); };

    // imgui demo window
    m_gui_context.delegates["demo"] = []{ if(DEMO_GUI){ ImGui::ShowDemoWindow(&DEMO_GUI); }};

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    m_camera = vk::PerspectiveCamera::create(m_window->aspect_ratio(), 45.f, .01f, 100.f);

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

        auto add_to_recent_files = [this](const std::string &f)
        {
            m_settings.recent_files.push_back(f);
            while(m_settings.recent_files.size() > 10){ m_settings.recent_files.pop_front(); }
        };

        switch(crocore::filesystem::get_file_type(f))
        {
            case crocore::filesystem::FileType::IMAGE:
                add_to_recent_files(f);
                load_environment(f);
                break;

            case crocore::filesystem::FileType::MODEL:
                add_to_recent_files(f);
                load_model(f);
                break;

            default:
                break;
        }
    };
    m_window->mouse_delegates["filedrop"] = file_drop_delegate;
}

void PBRViewer::create_camera_controls()
{
    // restore settings
    m_camera_control.orbit = m_settings.orbit_camera;
    m_camera_control.orbit->screen_size = m_window->size();
    m_camera_control.orbit->enabled = true;

    m_camera_control.fly = m_settings.fly_camera;

    if(m_settings.use_fly_camera){ m_camera_control.current = m_camera_control.fly; }
    else{ m_camera_control.current = m_camera_control.orbit; }

    // attach arcball mouse delegate
    auto arcball_delegeate = m_camera_control.orbit->mouse_delegate();
    arcball_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.orbit;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->mouse_delegates["orbit"] = std::move(arcball_delegeate);
    m_window->joystick_delegates["orbit"] = m_camera_control.orbit->joystick_delegate();

    auto flycamera_delegeate = m_camera_control.fly->mouse_delegate();
    flycamera_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->mouse_delegates["flycamera"] = std::move(flycamera_delegeate);

    auto fly_key_delegeate = m_camera_control.fly->key_delegate();
    fly_key_delegeate.enabled = [this]()
    {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->key_delegates["flycamera"] = std::move(fly_key_delegeate);
    m_window->joystick_delegates["flycamera"] = m_camera_control.fly->joystick_delegate();

    // update camera with arcball
    auto transform_cb = [this](const glm::mat4 &transform)
    {
        m_camera->set_global_transform(transform);
        if(m_path_tracer){ m_path_tracer->reset_accumulator(); }
    };
    m_camera_control.orbit->transform_cb = transform_cb;
    m_camera_control.fly->transform_cb = transform_cb;

    // update camera from current
    m_camera->set_global_transform(m_camera_control.current->transform());
}