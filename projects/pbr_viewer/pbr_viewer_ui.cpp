//
// Created by crocdialer on 2/11/22.
//

#include "pbr_viewer.hpp"

#include "ImGuiFileDialog.h"
#include <crocore/filesystem.hpp>
#include <glm/gtc/random.hpp>
#include <vierkant/imgui/imgui_util.h>

ImGuiFileDialog g_file_dialog;
constexpr char g_imgui_file_dialog_load_key[] = "imgui_file_dialog_load_key";
constexpr char g_imgui_file_dialog_save_key[] = "imgui_file_dialog_save_key";

bool DEMO_GUI = false;

void PBRViewer::toggle_ortho_camera()
{
    bool ortho = static_cast<bool>(std::dynamic_pointer_cast<vierkant::OrthoCamera>(m_camera));

    if(!ortho)
    {
        vierkant::ortho_camera_params_t params = {};
        params.near_ = 0.f;
        params.far_ = 10000.f;
        m_camera = vierkant::OrthoCamera::create(m_scene->registry(), params);
        m_camera->name = "ortho";
    }
    else
    {
        m_camera = vierkant::PerspectiveCamera::create(m_scene->registry(), {});
        m_camera->name = "default";
    }
    m_camera_control.current->transform_cb(m_camera_control.current->transform());
}

void PBRViewer::create_ui()
{
    // create a KeyDelegate
    vierkant::key_delegate_t key_delegate = {};
    key_delegate.key_press = [this](const vierkant::KeyEvent &e) {
        if(!m_settings.draw_ui || !(m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureKeyboard))
        {
            if(e.is_control_down())
            {
                switch(e.code())
                {
                    // save settings and scene
                    case vierkant::Key::_S:
                        save_settings(m_settings);
                        save_scene();
                        break;

                    // copy
                    case vierkant::Key::_C: m_copy_objects = m_selected_objects; break;

                    // paste
                    case vierkant::Key::_V:
                    {
                        for(const auto &obj: m_copy_objects) { m_scene->add_object(obj->clone()); }
                        break;
                    }

                    // group
                    case vierkant::Key::_G:
                    {
                        auto group = vierkant::Object3D::create(m_scene->registry(), "group");
                        m_scene->add_object(group);
                        for(const auto &sel_obj: m_selected_objects) { group->add_child(sel_obj); }
                        break;
                    }

                    case vierkant::Key::_A:
                    {
                        // select all
                        auto obj_view = m_scene->registry()->view<vierkant::Object3D *, vierkant::mesh_component_t>();
                        for(const auto &[entity, obj, mesh_cmp]: obj_view.each())
                        {
                            m_selected_objects.insert(obj->shared_from_this());
                        }
                        break;
                    }
                    default: break;
                }
                return;
            }

            switch(e.code())
            {
                case vierkant::Key::_Q: m_settings.current_guizmo = vierkant::gui::GuizmoType::INACTIVE; break;
                case vierkant::Key::_W: m_settings.current_guizmo = vierkant::gui::GuizmoType::TRANSLATE; break;
                case vierkant::Key::_E: m_settings.current_guizmo = vierkant::gui::GuizmoType::SCALE; break;
                case vierkant::Key::_R: m_settings.current_guizmo = vierkant::gui::GuizmoType::ROTATE; break;

                case vierkant::Key::_ESCAPE: running = false; break;

                case vierkant::Key::_SPACEBAR: m_settings.draw_ui = !m_settings.draw_ui; break;

                case vierkant::Key::_F:
                {
                    size_t monitor_index = m_window->monitor_index();
                    m_window->set_fullscreen(!m_window->fullscreen(), monitor_index);
                }
                break;
                case vierkant::Key::_H:
                {
                    m_window->set_cursor_visible(!m_window->cursor_visible());
                }
                break;
                case vierkant::Key::_C:
                {
                    if(m_camera_control.current == m_camera_control.orbit)
                    {
                        m_camera_control.current = m_camera_control.fly;
                    }
                    else { m_camera_control.current = m_camera_control.orbit; }
                    m_camera->transform = m_camera_control.current->transform();
                    if(m_path_tracer) { m_path_tracer->reset_accumulator(); }
                    break;
                }

                case vierkant::Key::_G: m_settings.draw_grid = !m_settings.draw_grid; break;

                case vierkant::Key::_P:
                    if(m_scene_renderer == m_pbr_renderer)
                    {
                        m_scene_renderer = m_path_tracer ? m_path_tracer : m_scene_renderer;
                    }
                    else { m_scene_renderer = m_pbr_renderer; }
                    break;

                case vierkant::Key::_B: m_settings.draw_aabbs = !m_settings.draw_aabbs; break;

                case vierkant::Key::_N: m_settings.draw_node_hierarchy = !m_settings.draw_node_hierarchy; break;

                case vierkant::Key::_M:
                    m_pbr_renderer->settings.debug_draw_ids = !m_pbr_renderer->settings.debug_draw_ids;
                    break;

                case vierkant::Key::_O: toggle_ortho_camera(); break;

                case vierkant::Key::_PERIOD:
                {
                    vierkant::AABB aabb;
                    for(const auto &obj: m_selected_objects) { aabb += obj->aabb().transform(obj->transform); }
                    m_camera_control.orbit->look_at = aabb.center();
                    if(m_camera_control.orbit->transform_cb)
                    {
                        m_camera_control.orbit->transform_cb(m_camera_control.orbit->transform());
                    }
                    break;
                }

                case vierkant::Key::_DELETE:
                case vierkant::Key::_BACKSPACE:
                    for(const auto &obj: m_selected_objects) { m_scene->remove_object(obj); }
                    m_selected_objects.clear();
                    break;
                default: break;
            }
        }
    };
    m_window->key_delegates[name()] = key_delegate;

    vierkant::joystick_delegate_t joystick_delegate = {};
    joystick_delegate.joystick_cb = [&](const auto &joysticks) {
        if(!joysticks.empty())
        {
            auto &js = joysticks.front();
            for(auto &[input, event]: js.input_events())
            {
                spdlog::trace("{}: {} {}", js.name(), vierkant::to_string(input),
                              (event == vierkant::Joystick::Event::BUTTON_PRESS ? " pressed" : " released"));

                if(event == vierkant::Joystick::Event::BUTTON_PRESS)
                {
                    switch(input)
                    {
                        case vierkant::Joystick::Input::BUTTON_MENU: m_settings.draw_ui = !m_settings.draw_ui; break;

                        case vierkant::Joystick::Input::BUTTON_X: m_settings.draw_grid = !m_settings.draw_grid; break;

                        case vierkant::Joystick::Input::BUTTON_Y:
                            if(m_scene_renderer == m_pbr_renderer)
                            {
                                m_scene_renderer = m_path_tracer ? m_path_tracer : m_scene_renderer;
                            }
                            else { m_scene_renderer = m_pbr_renderer; }
                            break;

                        case vierkant::Joystick::Input::BUTTON_A:
                            m_pbr_renderer->settings.debug_draw_ids = !m_pbr_renderer->settings.debug_draw_ids;
                            break;

                        case vierkant::Joystick::Input::BUTTON_B:
                            m_pbr_renderer->settings.use_meshlet_pipeline =
                                    !m_pbr_renderer->settings.use_meshlet_pipeline;
                            break;

                        case vierkant::Joystick::Input::BUTTON_BUMPER_RIGHT: toggle_ortho_camera(); break;

                        case vierkant::Joystick::Input::BUTTON_BACK:
                            if(m_camera_control.current == m_camera_control.orbit)
                            {
                                m_camera_control.current = m_camera_control.fly;
                            }
                            else { m_camera_control.current = m_camera_control.orbit; }
                            m_camera->transform = m_camera_control.current->transform();
                            if(m_path_tracer) { m_path_tracer->reset_accumulator(); }
                            break;

                        default: break;
                    }
                }
            }
        }
    };
    m_window->joystick_delegates[name()] = joystick_delegate;

    // try to fetch a font-file via http
    //    auto http_response = netzer::http::get(g_font_url);
    //    if(http_response.status_code != 200) { spdlog::warn("failed fetching a font from: {}", g_font_url); }

    // create a gui and add a draw-delegate
    vierkant::gui::Context::create_info_t gui_create_info = {};
    gui_create_info.ui_scale = m_settings.ui_scale;
    if(!m_settings.font_url.empty())
    {
        try
        {
            gui_create_info.font_data = crocore::filesystem::read_binary_file(m_settings.font_url);
        } catch(std::exception &e)
        {
            spdlog::warn(e.what());
        }
    }
    gui_create_info.font_size = m_settings.ui_font_scale;
    m_gui_context = vierkant::gui::Context(m_device, gui_create_info);

    float bg_alpha = .3f, bg_alpha_active = .9f;
    ImVec4 *colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, bg_alpha);
    colors[ImGuiCol_TitleBg] = ImVec4(0, 0, 0, bg_alpha);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0, 0, 0, bg_alpha_active);

    m_gui_context.delegates["application"].fn = [this] {
        int corner = 0;

        // const float DISTANCE = 10.0f;
        // ImGuiIO &io = ImGui::GetIO();

        ImVec2 window_pos(0, 0);
        // ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);
        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always);
        // ImGui::SetNextWindowSize(io.DisplaySize);

        ImGui::Begin("about: blank", nullptr,
                     (corner != -1 ? ImGuiWindowFlags_NoMove : 0) | ImGuiWindowFlags_NoDecoration |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoBringToFrontOnFocus);

        if(ImGui::BeginMenuBar())
        {
            if(ImGui::BeginMenu(name().c_str()))
            {
                ImGui::Separator();
                ImGui::Spacing();

                if(ImGui::MenuItem("save"))
                {
                    save_settings(m_settings);
                    save_scene();
                }

                if(ImGui::MenuItem("save as ..."))
                {
                    IGFD::FileDialogConfig config;
                    config.path = ".";
                    if(!m_settings.recent_files.empty())
                    {
                        config.path = crocore::filesystem::get_directory_part(*m_settings.recent_files.rbegin());
                    }
                    config.flags = ImGuiFileDialogFlags_DisableCreateDirectoryButton;
                    constexpr char filter_str[] = "vierkant-scene (*.json){.json}";
                    g_file_dialog.OpenDialog(g_imgui_file_dialog_save_key, "save scene ...", filter_str, config);
                }

                ImGui::Separator();
                ImGui::Spacing();

                if(ImGui::MenuItem("load ..."))
                {
                    IGFD::FileDialogConfig config;
                    config.path = ".";
                    if(!m_settings.recent_files.empty())
                    {
                        config.path = crocore::filesystem::get_directory_part(*m_settings.recent_files.rbegin());
                    }
                    config.flags = ImGuiFileDialogFlags_DisableCreateDirectoryButton;
                    constexpr char filter_str[] =
                            "supported (*.gltf *.glb *.obj *.hdr *.jpg *.png *.json){.gltf, .glb, .obj, .hdr, "
                            ".jpg, .png, .json},all {.*}";
                    g_file_dialog.OpenDialog(g_imgui_file_dialog_load_key, "load model/image/scene ...", filter_str,
                                             config);
                }

                if(ImGui::MenuItem("reload"))
                {
                    spdlog::warn("menu: reload");
                    if(auto settings = load_settings()) { m_settings = std::move(*settings); }
                    create_camera_controls();
                    if(m_settings.path_tracing) { m_scene_renderer = m_path_tracer; }
                    else { m_scene_renderer = m_pbr_renderer; }
                }
                ImGui::Separator();
                ImGui::Spacing();

                if(ImGui::BeginMenu("recent files"))
                {
                    for(const auto &f: m_settings.recent_files)
                    {
                        auto file_name = crocore::filesystem::get_filename_part(f);
                        if(ImGui::MenuItem(file_name.c_str()))
                        {
                            spdlog::debug("menu: open recent file -> {}", f);
                            load_file(f);
                            break;
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                ImGui::Spacing();

                if(ImGui::BeginMenu("settings"))
                {
                    const char *log_items[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"};
                    int log_level = static_cast<int>(spdlog::get_level());

                    if(ImGui::Combo("log level", &log_level, log_items, IM_ARRAYSIZE(log_items)))
                    {
                        spdlog::set_level(spdlog::level::level_enum(log_level));
                    }

                    ImGui::Checkbox("draw grid", &m_settings.draw_grid);
                    ImGui::Checkbox("draw aabbs", &m_settings.draw_aabbs);
                    ImGui::Checkbox("draw view-controls", &m_settings.ui_draw_view_controls);
                    ImGui::Checkbox("physics debug-draw", &m_settings.draw_physics);
                    ImGui::Checkbox("draw node hierarchy", &m_settings.draw_node_hierarchy);
                    ImGui::Checkbox("texture compression", &m_settings.texture_compression);
                    ImGui::Checkbox("remap indices", &m_settings.mesh_buffer_params.remap_indices);
                    ImGui::Checkbox("optimize vertex cache", &m_settings.mesh_buffer_params.optimize_vertex_cache);
                    ImGui::Checkbox("generate mesh-LODs", &m_settings.mesh_buffer_params.generate_lods);
                    ImGui::Checkbox("generate meshlets", &m_settings.mesh_buffer_params.generate_meshlets);
                    ImGui::Checkbox("cache mesh-bundles", &m_settings.cache_mesh_bundles);
                    ImGui::Checkbox("zip-compress bundles", &m_settings.cache_zip_archive);

                    ImGui::Separator();
                    ImGui::Spacing();


                    if(ImGui::RadioButton("none", m_settings.object_overlay_mode == vierkant::ObjectOverlayMode::None))
                    {
                        m_settings.object_overlay_mode = vierkant::ObjectOverlayMode::None;
                    }
                    ImGui::SameLine();

                    if(ImGui::RadioButton("mask", m_settings.object_overlay_mode == vierkant::ObjectOverlayMode::Mask))
                    {
                        m_settings.object_overlay_mode = vierkant::ObjectOverlayMode::Mask;
                    }
                    ImGui::SameLine();

                    if(ImGui::RadioButton("silhoutte",
                                          m_settings.object_overlay_mode == vierkant::ObjectOverlayMode::Silhouette))
                    {
                        m_settings.object_overlay_mode = vierkant::ObjectOverlayMode::Silhouette;
                    }

                    ImGui::Separator();
                    ImGui::Spacing();

                    // camera control select
                    bool orbit_cam = m_camera_control.current == m_camera_control.orbit, refresh = false;

                    if(ImGui::RadioButton("orbit", orbit_cam))
                    {
                        m_camera_control.current = m_camera_control.orbit;
                        refresh = true;
                    }
                    ImGui::SameLine();

                    if(ImGui::RadioButton("fly", !orbit_cam))
                    {
                        m_camera_control.current = m_camera_control.fly;
                        refresh = true;
                    }
                    ImGui::SameLine();
                    bool ortho = static_cast<bool>(std::dynamic_pointer_cast<vierkant::OrthoCamera>(m_camera));

                    if(ImGui::Checkbox("ortho", &ortho)) { toggle_ortho_camera(); }
                    if(refresh)
                    {
                        m_camera->transform = m_camera_control.current->transform();
                        if(m_path_tracer) { m_path_tracer->reset_accumulator(); }
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::Spacing();
                if(ImGui::Button("about: blank object"))
                {
                    auto new_obj = vierkant::Object3D::create(m_scene->registry());
                    new_obj->name = spdlog::fmt_lib::format("blank_{}", new_obj->id() % 1000);
                    m_scene->add_object(new_obj);
                }
                if(ImGui::Button("add boxes (25)"))
                {
                    auto cubes = m_scene->any_object_by_name("cubes");
                    if(!cubes)
                    {
                        auto new_group = vierkant::Object3D::create(m_scene->registry(), "cubes");
                        m_scene->add_object(new_group);
                        cubes = new_group.get();
                    }

                    for(uint32_t i = 0; i < 25; ++i)
                    {
                        auto new_obj = m_scene->create_mesh_object({m_box_mesh});
                        new_obj->name = spdlog::fmt_lib::format("cube_{}", new_obj->id() % 1000);
                        new_obj->transform.translation.y = 10.f;
                        new_obj->transform.translation += glm::ballRand(1.f);
                        vierkant::object_component auto &cmp = new_obj->add_component<vierkant::physics_component_t>();
                        vierkant::collision::box_t box = {m_box_mesh->entries.front().bounding_box.half_extents()};
                        cmp.shape = box;
                        cmp.mass = 1.f;

                        // add to group
                        cubes->add_child(new_obj);
                        cubes->name = spdlog::fmt_lib::format("cubes ({})", cubes->children.size());
                    }
                }

                ImGui::Separator();
                ImGui::Spacing();
                if(ImGui::MenuItem("quit")) { running = false; }
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("display"))
            {
                vierkant::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("renderer"))
            {
                bool is_path_tracer = m_scene_renderer == m_path_tracer;

                if(ImGui::RadioButton("pbr-deferred", !is_path_tracer)) { m_scene_renderer = m_pbr_renderer; }
                ImGui::SameLine();
                if(ImGui::RadioButton("pathtracer", is_path_tracer))
                {
                    m_scene_renderer = m_path_tracer ? m_path_tracer : m_scene_renderer;
                }
                ImGui::Spacing();
                vierkant::gui::draw_scene_renderer_settings_ui(m_scene_renderer);
                ImGui::EndMenu();
            }

            if(ImGui::BeginMenu("stats"))
            {
                auto loop_time = current_loop_time();
                ImGui::Text("fps: %.1f (%.1f ms)", 1.f / loop_time, loop_time * 1000.f);
                ImGui::Spacing();
                ImGui::Text("time: %s | frame: %d",
                            crocore::secs_to_time_str(static_cast<float>(application_time())).c_str(),
                            static_cast<uint32_t>(m_window->num_frames()));
                ImGui::Spacing();

                vierkant::gui::draw_scene_renderer_statistics_ui(m_scene_renderer);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();
    };

    // renderer window
    m_gui_context.delegates["file_dialog"].fn = [this] {
        // display
        ImGuiIO &io = ImGui::GetIO();
        ImGuiWindowFlags flags = 0;
        auto min_size = io.DisplaySize * 0.5f;

        // load dialog
        if(g_file_dialog.Display(g_imgui_file_dialog_load_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
                auto p = std::filesystem::path(g_file_dialog.GetCurrentPath()) /
                         std::filesystem::path(g_file_dialog.GetCurrentFileName());

                // load file
                load_file(p.string());
            }
            g_file_dialog.Close();
        }

        // save dialog
        if(g_file_dialog.Display(g_imgui_file_dialog_save_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
                auto p = std::filesystem::path(g_file_dialog.GetCurrentPath()) /
                         std::filesystem::path(g_file_dialog.GetCurrentFileName());

                // save scene
                save_scene(p.string());
            }
            g_file_dialog.Close();
        };
    };

    // log window
    m_gui_context.delegates["logger"].fn = [&log_queue = m_log_queue, &mutex = m_log_queue_mutex] {
        std::shared_lock lock(mutex);
        vierkant::gui::draw_logger_ui(log_queue);
    };

    // scenegraph window
    m_gui_context.delegates["scenegraph"].fn = [this] {
        ImGui::SetNextWindowPos(ImVec2(1470, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(440, 650), ImGuiCond_FirstUseEver);

        vierkant::gui::draw_scene_ui(m_scene, m_camera, &m_selected_objects);
    };

    // object/view manipulation
    m_gui_context.delegates["guizmo"].fn = [this] {
        if(!m_selected_objects.empty())
        {
            vierkant::gui::draw_transform_guizmo(*m_selected_objects.begin(), m_camera, m_settings.current_guizmo);
        }

        if(m_settings.ui_draw_view_controls)
        {
            auto view = vierkant::mat4_cast(m_camera->view_transform());
            const glm::vec2 sz = {150, 150};
            glm::vec2 pos = {(static_cast<float>(m_window->size().x) - sz.x) / 2.f, 0.f};
            if(ImGuizmo::ViewManipulate(glm::value_ptr(view), 1.f, {pos.x, pos.y}, {sz.x, sz.y}, 0x00000000))
            {
                auto transform = vierkant::inverse(vierkant::transform_cast(view));
                glm::vec3 pitch_yaw = glm::eulerAngles(transform.rotation);

                // account for roll and negative angles
                auto sng_x = static_cast<float>(crocore::sgn(-pitch_yaw.x));
                float sng_y = 1.f - 2.f * std::abs(pitch_yaw.z) * glm::one_over_pi<float>();
                pitch_yaw.x += std::abs(pitch_yaw.z) * sng_x;
                pitch_yaw.y = std::fmod(glm::two_pi<float>() + std::abs(pitch_yaw.z) + pitch_yaw.y * sng_y,
                                        glm::two_pi<float>());

                if(m_camera_control.current == m_camera_control.orbit)
                {
                    m_camera_control.orbit->spherical_coords = pitch_yaw;
                }
                else { m_camera_control.fly->spherical_coords = pitch_yaw; }

                if(m_camera_control.current->transform_cb)
                {
                    m_camera_control.current->transform_cb(m_camera_control.current->transform());
                }
            }
        }
    };

    // imgui demo window
    m_gui_context.delegates["demo"].fn = [] {
        if(DEMO_GUI) { ImGui::ShowDemoWindow(&DEMO_GUI); }
        if(DEMO_GUI) { ImPlot::ShowDemoWindow(&DEMO_GUI); }
    };

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    if(m_settings.ortho_camera) {}
    else
    {
        m_camera = vierkant::PerspectiveCamera::create(m_scene->registry(), {});
        m_camera->name = "default";
    }

    create_camera_controls();

    vierkant::mouse_delegate_t simple_mouse = {};
    simple_mouse.mouse_press = [this](const vierkant::MouseEvent &e) {
        if(!m_settings.draw_ui || !(m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse))
        {
            if(e.is_right())
            {
                m_selected_objects.clear();
                m_selected_indices.clear();
            }
            else if(e.is_left())
            {
                vierkant::Object3DPtr picked_object;
                //                picked_object = m_scene->pick(m_camera->calculate_ray(e.position(), m_window->size()));

                // TODO: gpu-based picking query - this is brute-force/blocking atm - only testing
                spdlog::stopwatch sw;
                auto picked_ids = m_scene_renderer->pick(glm::vec2(e.position()) / glm::vec2(m_window->size()), {});

                for(auto obj_id: picked_ids)
                {
                    spdlog::trace("obj_id: {} -- {}", std::to_string(obj_id),
                                  std::chrono::duration_cast<std::chrono::microseconds>(sw.elapsed()));

                    const auto &overlay_asset = m_overlay_assets[m_window->swapchain().image_index()];
                    if(overlay_asset.object_by_index_fn)
                    {
                        auto [object_id, sub_entry] = overlay_asset.object_by_index_fn(obj_id);
                        picked_object = m_scene->object_by_id(object_id)->shared_from_this();
                    }
                    spdlog::trace("picked object: {}", picked_object->name);
                    m_selected_indices.insert(obj_id);
                }

                if(picked_object)
                {
                    if(e.is_control_down())
                    {
                        if(m_selected_objects.contains(picked_object)) { m_selected_objects.erase(picked_object); }
                        else { m_selected_objects.insert(picked_object); }
                    }
                    else { m_selected_objects = {picked_object}; }
                }
            }
        }
    };

    m_window->mouse_delegates["simple_mouse"] = simple_mouse;

    // attach drag/drop mouse-delegate
    vierkant::mouse_delegate_t file_drop_delegate = {};
    file_drop_delegate.file_drop = [this](const vierkant::MouseEvent &, const std::vector<std::string> &files) {
        auto &f = files.back();
        load_file(f);
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

    if(m_settings.use_fly_camera) { m_camera_control.current = m_camera_control.fly; }
    else { m_camera_control.current = m_camera_control.orbit; }

    // attach arcball mouse delegate
    auto arcball_delegeate = m_camera_control.orbit->mouse_delegate();
    arcball_delegeate.enabled = [this]() {
        bool is_active = m_camera_control.current == m_camera_control.orbit;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->mouse_delegates["orbit"] = std::move(arcball_delegeate);
    m_window->joystick_delegates["orbit"] = m_camera_control.orbit->joystick_delegate();

    auto flycamera_delegeate = m_camera_control.fly->mouse_delegate();
    flycamera_delegeate.enabled = [this]() {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->mouse_delegates["flycamera"] = std::move(flycamera_delegeate);

    auto fly_key_delegeate = m_camera_control.fly->key_delegate();
    fly_key_delegeate.enabled = [this]() {
        bool is_active = m_camera_control.current == m_camera_control.fly;
        bool ui_captured =
                m_settings.draw_ui && m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse;
        return is_active && !ui_captured;
    };
    m_window->key_delegates["flycamera"] = std::move(fly_key_delegeate);
    m_window->joystick_delegates["flycamera"] = m_camera_control.fly->joystick_delegate();

    // update camera with arcball
    auto transform_cb = [this](const vierkant::transform_t &transform) {
        m_camera->set_global_transform(transform);
        if(m_path_tracer) { m_path_tracer->reset_accumulator(); }

        if(m_camera_control.current == m_camera_control.orbit)
        {
            if(auto ortho_cam = std::dynamic_pointer_cast<vierkant::OrthoCamera>(m_camera))
            {
                // default horizontal fov of perspective-view
                constexpr float default_hfov = 0.6912f;
                float aspect = m_window->aspect_ratio();
                float size = m_camera_control.orbit->distance * std::tan(0.5f * default_hfov / aspect);
                ortho_cam->ortho_params.top = size;
                ortho_cam->ortho_params.bottom = -size;
                ortho_cam->ortho_params.left = -size * aspect;
                ortho_cam->ortho_params.right = size * aspect;
            }
        }
    };
    m_camera_control.orbit->transform_cb = transform_cb;
    m_camera_control.fly->transform_cb = transform_cb;

    // update camera from current
    m_camera->transform = m_camera_control.current->transform();
}