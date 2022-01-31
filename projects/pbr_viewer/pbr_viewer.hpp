//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <filesystem>

#include <crocore/Application.hpp>
#include <vierkant/vierkant.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include "serialization.hpp"

const int WIDTH = 1920;
const int HEIGHT = 1080;
const bool V_SYNC = true;
bool DEMO_GUI = false;

const char *g_texture_url = "http://roa.h-cdn.co/assets/cm/14/47/1024x576/546b32b33240f_-_hasselhoff_kr_pr_nbc-lg.jpg";

const char *g_font_path = "/usr/local/share/fonts/Courier New Bold.ttf";
//const char *g_font_path = "/home/crocdialer/Desktop/Ubuntu-Medium.ttf";

//const char *g_font_path = "https://github.com/google/fonts/raw/main/ufl/ubuntu/Ubuntu-Medium.ttf";

///////////////////////////////////////////////////////////////////////////////////////////////////

class PBRViewer : public crocore::Application
{

public:

    struct settings_t
    {
        crocore::Severity log_severity = crocore::Severity::DEBUG;
        std::string model_path;
        std::string environment_path;

        vierkant::Window::create_info_t window_info =
                {
                        .instance = VK_NULL_HANDLE,
                        .size = {1920, 1080},
                        .position = {},
                        .fullscreen = false,
                        .vsync = true
                };

        vierkant::PBRDeferred::settings_t pbr_settings = {};
        vierkant::PBRPathTracer::settings_t path_tracer_settings = {};

        bool draw_aabbs = true;

        bool draw_node_hierarchy = true;

        bool path_tracing = false;

        bool texture_compression = false;

        bool fly_camera = false;

        glm::quat view_rotation = {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 view_look_at = {};
        float view_distance = 5.f;
    };

    explicit PBRViewer(int argc = 0, char *argv[] = nullptr) : crocore::Application(argc, argv){};

private:

    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override;

    vierkant::window_delegate_t::draw_result_t draw(const vierkant::WindowPtr &w);

    void create_context_and_window();

    void create_graphics_pipeline();

    void create_ui();

    void create_camera_controls();

    void create_texture_image();

    void load_model(const std::string &path = "");

    void load_environment(const std::string &path);

    void save_settings(settings_t settings, const std::filesystem::path &path = "settings.json") const;

    static settings_t load_settings(const std::filesystem::path &path = "settings.json");

    std::atomic<uint32_t> m_num_loading = 0;

    std::atomic<uint32_t> m_num_drawcalls = 0;

    settings_t m_settings;

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    VkQueue m_queue_loading = VK_NULL_HANDLE, m_queue_path_tracer = VK_NULL_HANDLE;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    vk::PerspectiveCameraPtr m_camera;

    struct camera_control_t
    {
        vierkant::OrbitCameraPtr orbit = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly = vierkant::FlyCamera::create();
        vierkant::CameraControlPtr current = orbit;
    } m_camera_control;

    std::set<vierkant::Object3DPtr> m_selected_objects;

    vk::PipelineCachePtr m_pipeline_cache;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    // selection of scene-renderers
    vierkant::PBRDeferredPtr m_pbr_renderer;

    vierkant::PBRPathTracerPtr m_path_tracer;

    vierkant::SceneRendererPtr m_scene_renderer;

    vk::Renderer m_renderer, m_renderer_overlay, m_renderer_gui;

    vierkant::FontPtr m_font;

    vierkant::gui::Context m_gui_context;

    vierkant::DrawContext m_draw_context;

};

int main(int argc, char *argv[])
{
    auto app = std::make_shared<PBRViewer>(argc, argv);
    return app->run();
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::settings_t &settings)
{

    ar(cereal::make_nvp("log_severity", settings.log_severity),
       cereal::make_nvp("model_path", settings.model_path),
       cereal::make_nvp("environment_path", settings.environment_path),
       cereal::make_nvp("window", settings.window_info),
       cereal::make_nvp("pbr_settings", settings.pbr_settings),
       cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
       cereal::make_nvp("draw_aabbs", settings.draw_aabbs),
       cereal::make_nvp("path_tracing", settings.path_tracing),
       cereal::make_nvp("texture_compression", settings.texture_compression),
       cereal::make_nvp("view_rotation", settings.view_rotation),
       cereal::make_nvp("view_look_at", settings.view_look_at),
       cereal::make_nvp("view_distance", settings.view_distance));
}

//
//} // namespace boost::serialization
