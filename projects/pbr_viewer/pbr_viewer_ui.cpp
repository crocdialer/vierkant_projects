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
constexpr char g_imgui_file_dialog_import_key[] = "imgui_file_dialog_import_key";
constexpr char g_imgui_file_dialog_import_as_mesh_lib_key[] = "g_imgui_file_dialog_import_as_mesh_lib_key";
constexpr char g_imgui_file_dialog_save_key[] = "imgui_file_dialog_save_key";

bool DEMO_GUI = false;

struct ui_state_t
{
    glm::ivec2 last_click;
};

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
    m_ui_state = {new ui_state_t, std::default_delete<ui_state_t>()};

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

                    // cut
                    case vierkant::Key::_X:
                        m_copy_objects = m_selected_objects;
                        for(const auto &obj: m_selected_objects) { obj->set_parent(nullptr); }
                        break;

                    // paste
                    case vierkant::Key::_V:
                    {
                        auto copy_dst = m_selected_objects.empty() ? m_scene->root() : *m_selected_objects.begin();
                        for(const auto &obj: m_copy_objects) { copy_dst->add_child(m_object_store->clone(obj.get())); }
                        break;
                    }

                    // group
                    case vierkant::Key::_G:
                    {
                        auto group = m_object_store->create_object();
                        group->name = "group";
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
                    for(const auto &obj: m_selected_objects) { aabb += obj->aabb().transform(obj->global_transform()); }
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
    gui_create_info.ini_file = true;
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

                //! file-load/import filter
                constexpr char filter_str[] =
                        "supported (*.gltf *.glb *.obj *.hdr *.jpg *.png *.json){.gltf, .glb, .obj, .hdr, "
                        ".jpg, .png, .json},all {.*}";
                auto get_file_dialog_config = [this] {
                    IGFD::FileDialogConfig config;
                    config.path = ".";
                    if(!m_settings.recent_files.empty())
                    {
                        config.path = crocore::filesystem::get_directory_part(*m_settings.recent_files.rbegin());
                    }
                    config.flags = ImGuiFileDialogFlags_DisableCreateDirectoryButton;
                    return config;
                };
                if(ImGui::MenuItem("load ..."))
                {
                    g_file_dialog.OpenDialog(g_imgui_file_dialog_load_key, "load model/image/scene ...", filter_str,
                                             get_file_dialog_config());
                }

                if(ImGui::MenuItem("import ..."))
                {
                    g_file_dialog.OpenDialog(g_imgui_file_dialog_import_key, "import model/image/scene ...", filter_str,
                                             get_file_dialog_config());
                }

                if(ImGui::MenuItem("import as mesh-library ..."))
                {
                    g_file_dialog.OpenDialog(g_imgui_file_dialog_import_as_mesh_lib_key,
                                             "import model as mesh-library ...", filter_str, get_file_dialog_config());
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
                            load_file(f, false);
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

                if(ImGui::BeginMenu("add"))
                {
                    if(ImGui::Button("empty object"))
                    {
                        auto new_obj = m_object_store->create_object();
                        new_obj->name = spdlog::fmt_lib::format("blank_{}", new_obj->id() % 1000);
                        m_scene->add_object(new_obj);
                    }

                    if(ImGui::Button("box"))
                    {
                        auto new_obj = m_scene->create_mesh_object({m_box_mesh});
                        new_obj->name = spdlog::fmt_lib::format("box_{}", new_obj->id() % 1000);
                        m_scene->add_object(new_obj);
                    }

                    // if(ImGui::Button("sphere"))
                    // {
                    //     auto new_obj = m_object_store->create_object();
                    //     new_obj->name = spdlog::fmt_lib::format("blank_{}", new_obj->id() % 1000);
                    //     m_scene->add_object(new_obj);
                    // }

                    if(ImGui::Button("physics boxes (25)"))
                    {
                        auto cubes = m_scene->any_object_by_name("cubes");
                        if(!cubes)
                        {
                            auto new_group = m_object_store->create_object();
                            new_group->name = "cubes";
                            m_scene->add_object(new_group);
                            cubes = new_group.get();
                        }

                        for(uint32_t i = 0; i < 25; ++i)
                        {
                            auto new_obj = m_scene->create_mesh_object({m_box_mesh});
                            new_obj->name = spdlog::fmt_lib::format("cube_{}", new_obj->id() % 1000);
                            new_obj->transform.translation.y = 10.f;
                            new_obj->transform.translation += glm::ballRand(1.f);
                            vierkant::object_component auto &cmp =
                                    new_obj->add_component<vierkant::physics_component_t>();
                            vierkant::collision::box_t box = {m_box_mesh->entries.front().bounding_box.half_extents()};
                            cmp.shape = box;
                            cmp.mass = 1.f;

                            // add to group
                            cubes->add_child(new_obj);
                            cubes->name = spdlog::fmt_lib::format("cubes ({})", cubes->children.size());
                        }
                    }
                    ImGui::EndMenu();
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

        auto p = std::filesystem::path(g_file_dialog.GetCurrentPath()) /
                 std::filesystem::path(g_file_dialog.GetCurrentFileName());

        // load dialog
        if(g_file_dialog.Display(g_imgui_file_dialog_load_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
                // clear scene, load file as one object
                load_file(p.string(), true);
            }
            g_file_dialog.Close();
        }

        // import dialog
        else if(g_file_dialog.Display(g_imgui_file_dialog_import_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
                // import file into scene, as one object
                load_file(p.string(), false);
            }
            g_file_dialog.Close();
        }

        // import as mesh-library dialog
        else if(g_file_dialog.Display(g_imgui_file_dialog_import_as_mesh_lib_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
                // import file into scene, as a library of objects
                add_to_recent_files(p);
                load_model_params_t load_params = {p};
                load_params.clear_scene = false;
                load_params.load_as_mesh_library = true;
                load_params.normalize_size = false;
                load_model(load_params);
            }
            g_file_dialog.Close();
        }

        // save dialog
        else if(g_file_dialog.Display(g_imgui_file_dialog_save_key, flags, min_size))
        {
            if(g_file_dialog.IsOk())
            {
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
        constexpr int corner = 1;
        const float DISTANCE = 10.0f;
        ImGuiIO &io = ImGui::GetIO();
        ImVec2 window_pos = ImVec2((corner & 1) ? io.DisplaySize.x - DISTANCE : DISTANCE,
                                (corner & 2) ? io.DisplaySize.y - DISTANCE : DISTANCE);
        ImVec2 window_pos_pivot = ImVec2((corner & 1) ? 1.0f : 0.0f, (corner & 2) ? 1.0f : 0.0f);

        ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
        vierkant::gui::draw_scene_ui(m_scene, m_camera, &m_selected_objects);
    };

    // object/view manipulation
    m_gui_context.delegates["guizmo"].fn = [this] {
        if(!m_selected_objects.empty())
        {
            vierkant::gui::draw_transform_guizmo(m_selected_objects, m_camera, m_settings.current_guizmo);
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
                // only store last click
                m_ui_state->last_click = e.position();
            }
        }
    };
    simple_mouse.mouse_release = [this](const vierkant::MouseEvent &e) {
        if(!m_settings.draw_ui || !(m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse))
        {
            if(e.is_left())
            {
                // clear selection area
                m_selection_area.reset();

                auto current_click = glm::clamp(e.position(), glm::ivec2(0), m_window->size() - 1);
                glm::vec2 tl = {std::min<int>(current_click.x, m_ui_state->last_click.x),
                                std::min<int>(current_click.y, m_ui_state->last_click.y)};
                glm::vec2 size = glm::abs(current_click - m_ui_state->last_click);
                auto picked_ids =
                        m_scene_renderer->pick(tl / glm::vec2(m_window->size()), size / glm::vec2(m_window->size()));

                std::unordered_set<vierkant::Object3D *> picked_objects;
                spdlog::stopwatch sw;

                for(uint32_t i = 0; i < picked_ids.size(); ++i)
                {
                    auto draw_idx = picked_ids[i];
                    vierkant::Object3D *picked_object = nullptr;
                    const auto &overlay_asset = m_overlay_assets[m_window->swapchain().image_index()];
                    if(overlay_asset.object_by_index_fn)
                    {
                        auto [object_id, sub_entry] = overlay_asset.object_by_index_fn(draw_idx);
                        picked_object = m_scene->object_by_id(object_id);
                        picked_objects.insert(picked_object);
                    }
                    spdlog::trace("picked object({}/{}): {}", i + 1, picked_ids.size(), picked_object->name);
                    m_selected_indices.insert(draw_idx);
                }

                // start new selection
                if(!e.is_control_down() && !picked_objects.empty()) { m_selected_objects.clear(); }

                for(auto *po: picked_objects)
                {
                    auto picked_object = po->shared_from_this();
                    if(e.is_control_down() && m_selected_objects.contains(picked_object))
                    {
                        m_selected_objects.erase(picked_object);
                    }
                    else { m_selected_objects.insert(picked_object); }
                }
            }
        }
    };

    simple_mouse.mouse_drag = [this](const vierkant::MouseEvent &e) {
        if(!m_settings.draw_ui || !(m_gui_context.capture_flags() & vierkant::gui::Context::WantCaptureMouse))
        {
            if(e.is_left())
            {
                glm::ivec2 tl = {std::min<int>(e.get_x(), m_ui_state->last_click.x),
                                 std::min<int>(e.get_y(), m_ui_state->last_click.y)};
                glm::ivec2 size = glm::abs(e.position() - m_ui_state->last_click);
                m_selection_area = {
                        tl.x,
                        tl.y,
                        size.x,
                        size.y,
                };
            }
        }
    };
    m_window->mouse_delegates["simple_mouse"] = simple_mouse;

    // attach drag/drop mouse-delegate
    vierkant::mouse_delegate_t file_drop_delegate = {};
    file_drop_delegate.file_drop = [this](const vierkant::MouseEvent &, const std::vector<std::string> &files) {
        auto &f = files.back();
        load_file(f, false);
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