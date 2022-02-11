//
// Created by crocdialer on 2/11/22.
//

#include <crocore/http.hpp>
#include <crocore/filesystem.hpp>
#include <vierkant/imgui/imgui_util.h>

#include "spdlog/sinks/base_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

#include "pbr_viewer.hpp"

bool DEMO_GUI = false;

using log_delegate_fn_t = std::function<void(const std::string &msg)>;

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
        for(const auto&[name, delegate] : log_delegates)
        {
            if(delegate){ delegate(fmt::to_string(formatted)); };
        }
    }

    void flush_() override{}
};

void PBRViewer::create_ui()
{
    auto imgui_sink = std::make_shared<delegate_sink_t>();
    spdlog::default_logger()->sinks().push_back(imgui_sink);

    log_delegate_fn_t imgui_log = [this](const std::string &msg)
    {
        m_log_queue.push_back(msg);
        while(m_log_queue.size() > m_max_log_queue_size){ m_log_queue.pop_front(); }
    };
    imgui_sink->log_delegates["imgui"] = imgui_log;

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
                LOG_TRACE << js.name() << " -- " << vierkant::to_string(input) << " "
                          << (event == vierkant::Joystick::Event::BUTTON_PRESS ? " pressed" : " released");

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
    m_gui_context.delegates["logger"] = [this]
    {
        int corner = 2;
        float bg_alpha = .2f;
        const float DISTANCE = 10.0f;
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE,
                                   (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        ImGui::SetNextWindowSizeConstraints(ImVec2(io.DisplaySize.x - 2 * DISTANCE, 220),
                                            ImVec2(io.DisplaySize.x - 2 * DISTANCE,
                                                   io.DisplaySize.y / 0.33f - 2 * DISTANCE));
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        ImGui::SetNextWindowBgAlpha(bg_alpha);

        bool show_logger = true;

        ImGui::Begin("logger", &show_logger, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                             ImGuiWindowFlags_NoNav);
        for(const auto &msg : m_log_queue)
        {
            ImGui::BulletText(msg.c_str());
            if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()){ ImGui::SetScrollHereY(1.0f); }
        }

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