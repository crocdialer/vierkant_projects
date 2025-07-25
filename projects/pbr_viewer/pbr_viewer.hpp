//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include "scene_data.hpp"
#include <crocore/Application.hpp>
#include <crocore/set_lru.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <vierkant/CameraControl.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/imgui/imgui_util.h>
#include <vierkant/object_overlay.hpp>
#include <vierkant/physics_context.hpp>
#include <vierkant/physics_debug_draw.hpp>

class PBRViewer : public crocore::Application
{

public:
    struct settings_t
    {
        spdlog::level::level_enum log_level = spdlog::level::info;
        std::string log_file;
        bool use_validation = false;
        bool use_debug_labels = false;
        crocore::set_lru<std::string> recent_files;

        vierkant::Window::create_info_t window_info = {.instance = VK_NULL_HANDLE,
                                                       .size = {1920, 1080},
                                                       .position = {},
                                                       .fullscreen = false,
                                                       .vsync = true,
                                                       .joysticks = true,
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

        std::string font_url;

        float ui_font_scale = 30.f;

        bool ui_draw_view_controls = false;

        bool draw_grid = true;

        bool draw_aabbs = false;

        bool draw_physics = false;

        bool draw_node_hierarchy = false;

        bool path_tracing = false;

        bool texture_compression = false;

        bool cache_mesh_bundles = false;

        bool cache_zip_archive = false;

        bool enable_raytracing_pipeline_features = true;

        bool enable_ray_query_features = true;

        bool enable_mesh_shader_device_features = true;

        vierkant::OrbitCameraPtr orbit_camera = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly_camera = vierkant::FlyCamera::create();
        bool use_fly_camera = false;
        bool ortho_camera = false;

        vierkant::gui::GuizmoType current_guizmo = vierkant::gui::GuizmoType::INACTIVE;

        vierkant::ObjectOverlayMode object_overlay_mode = vierkant::ObjectOverlayMode::Mask;

        //! desired fps, default: 0.f (disable throttling)
        float target_fps = 60.f;
    };

    static constexpr char s_default_scene_path[] = "scene.json";

    explicit PBRViewer(const crocore::Application::create_info_t &create_info);

    void load_file(const std::string &path, bool clear);

    bool parse_override_settings(int argc, char *argv[]);

private:
    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override;

    vierkant::window_delegate_t::draw_result_t draw(const vierkant::WindowPtr &w);

    void init_logger();

    void create_context_and_window();

    void create_graphics_pipeline();

    void create_ui();

    void create_camera_controls();

    void create_texture_image();

    void add_to_recent_files(const std::filesystem::path &f);

    struct load_model_params_t
    {
        //! model-path
        std::filesystem::path path = {};

        //! load a model as mesh-library, containing individual sub-object per mesh-entry
        bool mesh_library = false;

        //! when loading as mesh-library, avoid duplicated objects for identical entries
        bool mesh_library_no_dups = false;

        //! normalize dimensions of loaded assets
        bool normalize_size = false;

        //! clear the scene when loading-operation succeeds
        bool clear_scene = false;
    };
    void load_model(const load_model_params_t &params);

    void load_environment(const std::string &path);

    void save_settings(settings_t settings, const std::filesystem::path &path = "settings.json") const;

    static std::optional<settings_t> load_settings(const std::filesystem::path &path = "settings.json");

    void save_asset_bundle(const vierkant::model::model_assets_t &mesh_assets, const std::filesystem::path &path);

    std::optional<vierkant::model::model_assets_t> load_asset_bundle(const std::filesystem::path &path);

    vierkant::MeshPtr load_mesh(const std::filesystem::path &path);

    void save_scene(std::filesystem::path path = {});

    static std::optional<scene_data_t> load_scene_data(const std::filesystem::path &path = s_default_scene_path);

    void build_scene(const std::optional<scene_data_t> &scene_data, bool import = false,
                     vierkant::SceneId scene_id = {});

    struct overlay_assets_t
    {
        vierkant::CommandBuffer command_buffer;
        vierkant::Semaphore semaphore;
        uint64_t semaphore_value = 0;
        vierkant::object_overlay_context_ptr object_overlay_context;
        vierkant::SceneRenderer::object_id_by_index_fn_t object_by_index_fn;
        vierkant::SceneRenderer::indices_by_id_fn_t indices_by_id_fn;
        vierkant::ImagePtr overlay;
    };

    vierkant::semaphore_submit_info_t generate_overlay(overlay_assets_t &overlay_asset,
                                                       const vierkant::ImagePtr &id_img);

    void toggle_ortho_camera();

    std::atomic<uint32_t> m_num_loading = 0, m_num_frames = 0;

    settings_t m_settings = {};

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    VkQueue m_queue_model_loading = VK_NULL_HANDLE, m_queue_image_loading = VK_NULL_HANDLE,
            m_queue_render = VK_NULL_HANDLE;

    // B10G11R11 saves 50% memory but now seeing more&more cases with strong banding-issues
    VkFormat m_hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;//VK_FORMAT_B10G11R11_UFLOAT_PACK32;

    VkBufferUsageFlags m_mesh_buffer_flags = 0;

    vierkant::mesh_map_t m_mesh_map;
    vierkant::MeshPtr m_box_mesh;
    vierkant::CollisionShapeId m_box_shape_id = vierkant::CollisionShapeId::nil();

    // window handle
    vierkant::WindowPtr m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    // init a scene with physics-support on application-threadpool
    std::shared_ptr<vierkant::ObjectStore> m_object_store = vierkant::create_object_store();
    std::shared_ptr<vierkant::PhysicsScene> m_scene = vierkant::PhysicsScene::create(m_object_store);
    vierkant::PhysicsDebugRendererPtr m_physics_debug;

    vierkant::CameraPtr m_camera;

    struct camera_control_t
    {
        vierkant::OrbitCameraPtr orbit = vierkant::OrbitCamera::create();
        vierkant::FlyCameraPtr fly = vierkant::FlyCamera::create();
        vierkant::CameraControlPtr current = orbit;
    } m_camera_control;

    // object-selection / copy/paste
    std::set<vierkant::Object3DPtr> m_selected_objects;
    std::set<vierkant::Object3DPtr> m_copy_objects;
    std::unordered_set<uint32_t> m_selected_indices;
    std::optional<crocore::Area_<int>> m_selection_area;

    vierkant::PipelineCachePtr m_pipeline_cache;

    // selection of scene-renderers
    vierkant::PBRDeferredPtr m_pbr_renderer;

    vierkant::PBRPathTracerPtr m_path_tracer;

    vierkant::SceneRendererPtr m_scene_renderer;

    vierkant::Rasterizer m_renderer, m_renderer_overlay, m_renderer_gui;

    std::vector<overlay_assets_t> m_overlay_assets;
    vierkant::ImagePtr m_object_id_image;

    vierkant::gui::Context m_gui_context;

    // some internal UI-state
    std::unique_ptr<struct ui_state_t, std::function<void(struct ui_state_t *)>> m_ui_state;

    vierkant::DrawContext m_draw_context;

    size_t m_max_log_queue_size = 100;
    std::deque<std::pair<std::string, spdlog::level::level_enum>> m_log_queue;
    std::shared_mutex m_log_queue_mutex, m_bundle_rw_mutex, m_mutex_semaphore_submit;
    std::map<std::string, std::shared_ptr<spdlog::logger>> _loggers;

    scene_data_t m_scene_data;

    // track of scene/model-paths
    std::map<vierkant::MeshId, std::filesystem::path> m_model_paths;
    std::map<vierkant::SceneId, std::filesystem::path> m_scene_paths;
    vierkant::SceneId m_scene_id;
};