//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <filesystem>

#include "serialization.hpp"
#include "spdlog/spdlog.h"
#include <crocore/Application.hpp>
#include <crocore/set_lru.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/vierkant.hpp>


constexpr char g_texture_url[] =
        "http://roa.h-cdn.co/assets/cm/14/47/1024x576/546b32b33240f_-_hasselhoff_kr_pr_nbc-lg.jpg";

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
        crocore::set_lru<std::string> recent_files;

        vierkant::Window::create_info_t window_info = {.instance = VK_NULL_HANDLE,
                                                       .size = {1920, 1080},
                                                       .position = {},
                                                       .fullscreen = false,
                                                       .vsync = true,
                                                       .monitor_index = 0,
                                                       .sample_count = VK_SAMPLE_COUNT_1_BIT,
                                                       .title = "pbr_viewer"};

        vierkant::PBRDeferred::settings_t pbr_settings = {};
        vierkant::PBRPathTracer::settings_t path_tracer_settings = {};

        bool draw_ui = true;

        float ui_scale = 1.f;

        bool draw_grid = true;

        bool draw_aabbs = false;

        bool draw_node_hierarchy = false;

        bool path_tracing = false;

        bool texture_compression = false;

        bool optimize_vertex_cache = true;

        bool generate_lods = false;

        bool generate_meshlets = false;

        bool cache_mesh_bundles = false;

        bool enable_raytracing_device_features = true;

        bool enable_mesh_shader_device_features = true;

        vierkant::OrbitCameraPtr orbit_camera = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly_camera = vierkant::FlyCamera::create();
        bool use_fly_camera = false;
        vierkant::physical_camera_params_t camera_params = {};

        vierkant::gui::GuizmoType current_guizmo = vierkant::gui::GuizmoType::INACTIVE;

        //! desired fps, default: 0.f (disable throttling)
        float target_fps = 60.f;
    };

    struct scene_node_t
    {
        std::string name;
        size_t mesh_index = std::numeric_limits<size_t>::max();
        vierkant::transform_t transform = {};
        std::optional<vierkant::animation_state_t> animation_state = {};
    };

    struct scene_data_t
    {
        std::vector<std::string> model_paths;
        std::string environment_path;
        std::vector<scene_node_t> nodes;
    };

    explicit PBRViewer(const crocore::Application::create_info_t &create_info);

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

    void load_model(const std::filesystem::path &path = {});

    void load_environment(const std::string &path);

    void save_settings(settings_t settings, const std::filesystem::path &path = "settings.json") const;

    static settings_t load_settings(const std::filesystem::path &path = "settings.json");

    void save_asset_bundle(const vierkant::model::asset_bundle_t &asset_bundle, const std::filesystem::path &path);

    std::optional<vierkant::model::asset_bundle_t> load_asset_bundle(const std::filesystem::path &path);

    vierkant::MeshPtr load_mesh(const std::filesystem::path &path);

    void save_scene(const std::filesystem::path &path = "scene.json") const;

    void load_scene(const std::filesystem::path &path = "scene.json");

    std::atomic<uint32_t> m_num_loading = 0;

    settings_t m_settings = {};

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    VkQueue m_queue_model_loading = VK_NULL_HANDLE, m_queue_image_loading = VK_NULL_HANDLE,
            m_queue_pbr_render = VK_NULL_HANDLE, m_queue_path_tracer = VK_NULL_HANDLE;

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

//    vierkant::DescriptorPoolPtr m_descriptor_pool;

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
    std::shared_mutex m_log_queue_mutex, m_bundle_rw_mutex;
    std::map<std::string, std::shared_ptr<spdlog::logger>> _loggers;

    // tmp, keep track of mesh/model-paths
    std::map<vierkant::MeshWeakPtr, std::filesystem::path, std::owner_less<vierkant::MeshWeakPtr>> m_model_paths;
};

template<class Archive>
void serialize(Archive &ar, PBRViewer::settings_t &settings)
{
    ar(cereal::make_nvp("log_level", settings.log_level), cereal::make_nvp("model_path", settings.model_path),
       cereal::make_nvp("environment_path", settings.environment_path),
       cereal::make_nvp("recent_files", settings.recent_files), cereal::make_nvp("window", settings.window_info),
       cereal::make_nvp("pbr_settings", settings.pbr_settings),
       cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
       cereal::make_nvp("draw_ui", settings.draw_ui), cereal::make_nvp("ui_scale", settings.ui_scale),
       cereal::make_nvp("draw_grid", settings.draw_grid), cereal::make_nvp("draw_aabbs", settings.draw_aabbs),
       cereal::make_nvp("draw_node_hierarchy", settings.draw_node_hierarchy),
       cereal::make_nvp("path_tracing", settings.path_tracing),
       cereal::make_nvp("texture_compression", settings.texture_compression),
       cereal::make_nvp("optimize_vertex_cache", settings.optimize_vertex_cache),
       cereal::make_nvp("generate_lods", settings.generate_lods),
       cereal::make_nvp("generate_meshlets", settings.generate_meshlets),
       cereal::make_nvp("cache_mesh_bundles", settings.cache_mesh_bundles),
       cereal::make_nvp("enable_raytracing_device_features", settings.enable_raytracing_device_features),
       cereal::make_nvp("enable_mesh_shader_device_features", settings.enable_mesh_shader_device_features),
       cereal::make_nvp("orbit_camera", settings.orbit_camera), cereal::make_nvp("fly_camera", settings.fly_camera),
       cereal::make_nvp("use_fly_camera", settings.use_fly_camera),
       cereal::make_nvp("camera_params", settings.camera_params),
       cereal::make_nvp("current_guizmo", settings.current_guizmo),
       cereal::make_nvp("target_fps", settings.target_fps));
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::scene_node_t &scene_node)
{
    ar(cereal::make_nvp("name", scene_node.name),
       cereal::make_nvp("mesh_index", scene_node.mesh_index),
       cereal::make_nvp("transform", scene_node.transform),
       cereal::make_nvp("animation_state", scene_node.animation_state));
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::scene_data_t &scene_data)
{
    ar(cereal::make_nvp("environment_path", scene_data.environment_path),
       cereal::make_nvp("model_paths", scene_data.model_paths),
       cereal::make_nvp("nodes", scene_data.nodes));
}