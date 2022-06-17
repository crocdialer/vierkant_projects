//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <filesystem>

#include "spdlog/spdlog.h"
#include <crocore/Application.hpp>
#include <crocore/SetLRU.h>
#include <vierkant/vierkant.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include "serialization.hpp"


constexpr char g_texture_url[] = "http://roa.h-cdn.co/assets/cm/14/47/1024x576/546b32b33240f_-_hasselhoff_kr_pr_nbc-lg.jpg";

constexpr char g_font_url[] = "https://fonts.gstatic.com/s/courierprime/v5/u-4k0q2lgwslOqpF_6gQ8kELY7pMf-c.ttf";

///////////////////////////////////////////////////////////////////////////////////////////////////

class PBRViewer : public crocore::Application
{

public:

    struct settings_t
    {
        spdlog::level::level_enum log_level = spdlog::level::info;
        std::string model_path;
        std::string environment_path;
        crocore::SetLRU<std::string> recent_files;

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

        bool draw_ui = true;

        bool draw_grid = true;

        bool draw_aabbs = false;

        bool draw_node_hierarchy = false;

        bool path_tracing = false;

        bool texture_compression = false;

        bool enable_raytracing_device_features = false;

        vierkant::OrbitCameraPtr orbit_camera = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly_camera = vierkant::FlyCamera::create();
        bool use_fly_camera = false;
        float fov = 45.f;

        //! desired fps, default: 0.f (disable throttling)
        float target_fps = 0.f;
    };

    explicit PBRViewer(const crocore::Application::create_info_t &create_info) : crocore::Application(create_info){};

    void load_file(const std::string &path);

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

    settings_t m_settings;

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    VkQueue m_queue_loading = VK_NULL_HANDLE, m_queue_pbr_render = VK_NULL_HANDLE, m_queue_path_tracer = VK_NULL_HANDLE;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    vierkant::PerspectiveCameraPtr m_camera;

    struct camera_control_t
    {
        vierkant::OrbitCameraPtr orbit = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly = vierkant::FlyCamera::create();
        vierkant::CameraControlPtr current = orbit;
    } m_camera_control;

    std::set<vierkant::Object3DPtr> m_selected_objects;

    vierkant::PipelineCachePtr m_pipeline_cache;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    // selection of scene-renderers
    vierkant::PBRDeferredPtr m_pbr_renderer;

    vierkant::PBRPathTracerPtr m_path_tracer;

    vierkant::SceneRendererPtr m_scene_renderer;

    vierkant::Renderer m_renderer, m_renderer_overlay, m_renderer_gui;

    vierkant::FontPtr m_font;

    vierkant::gui::Context m_gui_context;

    vierkant::DrawContext m_draw_context;

    size_t m_max_log_queue_size = 100;
    std::deque<std::pair<std::string, spdlog::level::level_enum>> m_log_queue;
    std::shared_mutex m_log_queue_mutex;
    std::map<std::string, std::shared_ptr<spdlog::logger>> _loggers;

    struct draw_call_status_t
    {
        uint32_t num_draws = 0;
        uint32_t num_frustum_culled = 0;
        uint32_t num_occlusion_culled = 0;
    };
    size_t m_max_draw_call_status_queue_size = 1000;
    std::deque<draw_call_status_t> m_draw_call_status_queue;
};

template<class Archive>
void serialize(Archive &ar, PBRViewer::settings_t &settings)
{
    ar(cereal::make_nvp("log_level", settings.log_level),
       cereal::make_nvp("model_path", settings.model_path),
       cereal::make_nvp("environment_path", settings.environment_path),
       cereal::make_nvp("recent_files", settings.recent_files),
       cereal::make_nvp("window", settings.window_info),
       cereal::make_nvp("pbr_settings", settings.pbr_settings),
       cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
       cereal::make_nvp("draw_ui", settings.draw_ui),
       cereal::make_nvp("draw_grid", settings.draw_grid),
       cereal::make_nvp("draw_aabbs", settings.draw_aabbs),
       cereal::make_nvp("draw_node_hierarchy", settings.draw_node_hierarchy),
       cereal::make_nvp("path_tracing", settings.path_tracing),
       cereal::make_nvp("texture_compression", settings.texture_compression),
       cereal::make_nvp("enable_raytracing_device_features", settings.enable_raytracing_device_features),
       cereal::make_nvp("orbit_camera", settings.orbit_camera),
       cereal::make_nvp("fly_camera", settings.fly_camera),
       cereal::make_nvp("use_fly_camera", settings.use_fly_camera),
       cereal::make_nvp("fov", settings.fov),
       cereal::make_nvp("target_fps", settings.target_fps));
}
