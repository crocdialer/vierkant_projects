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
#include <vierkant/object_overlay.hpp>
#include <vierkant/imgui/imgui_util.h>
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

        vierkant::mesh_buffer_params_t mesh_buffer_params = {.remap_indices = false,
                                                             .optimize_vertex_cache = true,
                                                             .generate_lods = false,
                                                             .generate_meshlets = false,
                                                             .pack_vertices = true};

        bool draw_ui = true;

        float ui_scale = 1.f;

        bool draw_grid = true;

        bool draw_aabbs = false;

        bool draw_node_hierarchy = false;

        bool path_tracing = false;

        bool texture_compression = false;

        bool cache_mesh_bundles = false;

        bool enable_raytracing_pipeline_features = true;

        bool enable_ray_query_features = true;

        bool enable_mesh_shader_device_features = true;

        vierkant::OrbitCameraPtr orbit_camera = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly_camera = vierkant::FlyCamera::create();
        bool use_fly_camera = false;

        vierkant::gui::GuizmoType current_guizmo = vierkant::gui::GuizmoType::INACTIVE;

        vierkant::ObjectOverlayMode object_overlay_mode = vierkant::ObjectOverlayMode::Mask;

        //! desired fps, default: 0.f (disable throttling)
        float target_fps = 60.f;
    };

    struct scene_node_t
    {
        std::string name;
        size_t mesh_index = std::numeric_limits<size_t>::max();

        //! optional set of enabled entries.
        std::optional<std::set<uint32_t>> entry_indices = {};

        vierkant::transform_t transform = {};
        std::optional<vierkant::animation_component_t> animation_state = {};
    };

    struct scene_camera_t
    {
        std::string name;
        vierkant::transform_t transform = {};
        vierkant::physical_camera_component_t params = {};
    };

    struct scene_data_t
    {
        std::vector<std::string> model_paths;
        std::string environment_path;
        std::vector<scene_node_t> nodes;
        std::vector<scene_camera_t> cameras;
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

    void save_asset_bundle(const vierkant::model::mesh_assets_t &mesh_assets, const std::filesystem::path &path);

    std::optional<vierkant::model::mesh_assets_t> load_asset_bundle(const std::filesystem::path &path);

    vierkant::MeshPtr load_mesh(const std::filesystem::path &path);

    void save_scene(const std::filesystem::path &path = "scene.json") const;

    static std::optional<scene_data_t> load_scene_data(const std::filesystem::path &path = "scene.json");

    void build_scene(const std::optional<scene_data_t> &scene_data);

    std::optional<uint16_t> mouse_pick_gpu(const glm::ivec2 &click_pos);

    struct overlay_assets_t
    {
        vierkant::CommandBuffer command_buffer;
        vierkant::Semaphore semaphore;
        vierkant::object_overlay_context_ptr object_overlay_context;
        vierkant::ImagePtr overlay;
    };

    vierkant::semaphore_submit_info_t generate_overlay(overlay_assets_t &overlay_asset, const vierkant::ImagePtr &id_img);

    std::atomic<uint32_t> m_num_loading = 0;

    settings_t m_settings = {};

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    VkQueue m_queue_model_loading = VK_NULL_HANDLE, m_queue_image_loading = VK_NULL_HANDLE,
            m_queue_pbr_render = VK_NULL_HANDLE, m_queue_path_tracer = VK_NULL_HANDLE;

    // B10G11R11 saves 50% memory but now seeing more&more cases with strong banding-issues
    VkFormat m_hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;//VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    vierkant::CameraPtr m_camera;

    struct camera_control_t
    {
        vierkant::OrbitCameraPtr orbit = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly = vierkant::FlyCamera::create();
        vierkant::CameraControlPtr current = orbit;
    } m_camera_control;

    std::set<vierkant::Object3DPtr> m_selected_objects;
    std::unordered_set<uint32_t> m_selected_indices;

    vierkant::PipelineCachePtr m_pipeline_cache;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    // selection of scene-renderers
    vierkant::PBRDeferredPtr m_pbr_renderer;

    vierkant::PBRPathTracerPtr m_path_tracer;

    vierkant::SceneRendererPtr m_scene_renderer;

    vierkant::Rasterizer m_renderer, m_renderer_overlay, m_renderer_gui;

    std::vector<overlay_assets_t> m_overlay_assets;
    vierkant::ImagePtr m_object_id_image;

    vierkant::gui::Context m_gui_context;

    vierkant::DrawContext m_draw_context;

    size_t m_max_log_queue_size = 100;
    std::deque<std::pair<std::string, spdlog::level::level_enum>> m_log_queue;
    std::shared_mutex m_log_queue_mutex, m_bundle_rw_mutex;
    std::map<std::string, std::shared_ptr<spdlog::logger>> _loggers;

    scene_data_t m_scene_data;

    // tmp, keep track of mesh/model-paths
    std::map<vierkant::MeshConstPtr, std::filesystem::path> m_model_paths;
};

template<class Archive>
void serialize(Archive &ar, PBRViewer::settings_t &settings)
{
    ar(cereal::make_nvp("log_level", settings.log_level), cereal::make_nvp("recent_files", settings.recent_files),
       cereal::make_nvp("window", settings.window_info), cereal::make_nvp("pbr_settings", settings.pbr_settings),
       cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
       cereal::make_nvp("draw_ui", settings.draw_ui), cereal::make_nvp("ui_scale", settings.ui_scale),
       cereal::make_nvp("draw_grid", settings.draw_grid), cereal::make_nvp("draw_aabbs", settings.draw_aabbs),
       cereal::make_nvp("draw_node_hierarchy", settings.draw_node_hierarchy),
       cereal::make_nvp("path_tracing", settings.path_tracing),
       cereal::make_nvp("texture_compression", settings.texture_compression),
       cereal::make_nvp("mesh_buffer_params", settings.mesh_buffer_params),
       cereal::make_nvp("cache_mesh_bundles", settings.cache_mesh_bundles),
       cereal::make_nvp("enable_raytracing_pipeline_features", settings.enable_raytracing_pipeline_features),
       cereal::make_nvp("enable_ray_query_features", settings.enable_ray_query_features),
       cereal::make_nvp("enable_mesh_shader_device_features", settings.enable_mesh_shader_device_features),
       cereal::make_nvp("orbit_camera", settings.orbit_camera), cereal::make_nvp("fly_camera", settings.fly_camera),
       cereal::make_nvp("use_fly_camera", settings.use_fly_camera),
       cereal::make_nvp("current_guizmo", settings.current_guizmo),
       cereal::make_nvp("object_overlay_mode", settings.object_overlay_mode),
       cereal::make_nvp("target_fps", settings.target_fps));
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::scene_node_t &scene_node)
{
    ar(cereal::make_nvp("name", scene_node.name), cereal::make_nvp("mesh_index", scene_node.mesh_index),
       cereal::make_nvp("entry_indices", scene_node.entry_indices), cereal::make_nvp("transform", scene_node.transform),
       cereal::make_nvp("animation_state", scene_node.animation_state));
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::scene_camera_t &camera)
{
    ar(cereal::make_nvp("name", camera.name), cereal::make_nvp("transform", camera.transform),
       cereal::make_nvp("params", camera.params));
}

template<class Archive>
void serialize(Archive &ar, PBRViewer::scene_data_t &scene_data)
{
    ar(cereal::make_nvp("environment_path", scene_data.environment_path),
       cereal::make_nvp("model_paths", scene_data.model_paths), cereal::make_nvp("nodes", scene_data.nodes),
       cereal::make_nvp("cameras", scene_data.cameras));
}