//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <crocore/Application.hpp>
#include <crocore/Animation.hpp>

#include <vierkant/vierkant.hpp>

#include <vierkant/RayBuilder.hpp>
#include <vierkant/RayTracer.hpp>

const int WIDTH = 1920;
const int HEIGHT = 1080;
const bool V_SYNC = true;
bool DEMO_GUI = true;

////////////////////////////// VALIDATION LAYER ///////////////////////////////////////////////////

#ifdef NDEBUG
const bool g_enable_validation_layers = false;
#else
const bool g_enable_validation_layers = true;
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////

class SimpleRayTracing : public crocore::Application
{

public:

    explicit SimpleRayTracing(int argc = 0, char *argv[] = nullptr) : crocore::Application(argc, argv){};

private:

    enum SemaphoreValue
    {
        RAYTRACING_FINISHED = 1,
        RENDER_FINISHED = 2
    };

    struct ray_assets_t
    {
        //! timeline semaphore to sync raytracing and draw-operations
        vierkant::Semaphore semaphore;

        //! records raytracing commands
        vierkant::CommandBuffer command_buffer;

        //! an accelaration structure and it's resources
        vierkant::RayBuilder::acceleration_asset_t acceleration_asset;

        vierkant::RayTracer::tracable_t tracable = {};
    };

    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override;

    std::vector<VkCommandBuffer> draw(const vierkant::WindowPtr &w);

    void create_context_and_window();

    void create_graphics_pipeline();

    void load_model(const std::filesystem::path &path = "");

    void load_environment(const std::filesystem::path &path);

    void update_trace_descriptors();

    bool m_use_msaa = false;

    bool m_fullscreen = false;

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    vk::PerspectiveCameraPtr m_camera;

    vk::MeshPtr m_mesh;

    vk::Renderer m_renderer, m_gui_renderer;

    // gui context
    vierkant::gui::Context m_gui_context;

    vierkant::DrawContext m_draw_context;

    std::vector<ray_assets_t> m_ray_assets;

    vierkant::RayBuilder m_ray_builder;

    //!
    vierkant::RayTracer m_ray_tracer;

    //! toggle raster / raytracer
    bool m_show_ray_tracer = true;

    // information about the raytracing-pipeline to run
    vierkant::RayTracer::tracable_t m_tracable = {};

    vierkant::ImagePtr m_storage_image;

    glm::mat4 m_model_transform;

    vk::Arcball m_arcball;

    vierkant::MeshNodePtr m_mesh_node;
    std::set<vierkant::Object3DPtr> m_selected_objects;
    vierkant::ScenePtr m_scene = vierkant::Scene::create();
};

int main(int argc, char *argv[])
{
    auto app = std::make_shared<SimpleRayTracing>(argc, argv);
    return app->run();
}
