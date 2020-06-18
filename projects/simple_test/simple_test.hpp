//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <crocore/Application.hpp>
#include <vierkant/vierkant.hpp>

const int WIDTH = 1920;
const int HEIGHT = 1080;
const bool V_SYNC = true;
bool DEMO_GUI = false;

////////////////////////////// VALIDATION LAYER ///////////////////////////////////////////////////

#ifdef NDEBUG
const bool g_enable_validation_layers = false;
#else
const bool g_enable_validation_layers = true;
#endif

const char *g_texture_url = "http://roa.h-cdn.co/assets/cm/14/47/1024x576/546b32b33240f_-_hasselhoff_kr_pr_nbc-lg.jpg";

const char *g_font_path = "/usr/local/share/fonts/Courier New Bold.ttf";

//const char *g_font_path = "https://github.com/google/fonts/raw/master/ufl/ubuntu/Ubuntu-Medium.ttf";

VkFormat vk_format(const crocore::ImagePtr &img, bool compress = true);

vierkant::MaterialPtr create_material(const vierkant::assimp::material_t &mat);

void render_scene(vierkant::Renderer &renderer, vierkant::ScenePtr scene, vierkant::CameraPtr camera);

///////////////////////////////////////////////////////////////////////////////////////////////////

class Vierkant3DViewer : public crocore::Application
{

public:

    explicit Vierkant3DViewer(int argc = 0, char *argv[] = nullptr) : crocore::Application(argc, argv){};

private:

    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override;

    std::vector<VkCommandBuffer> draw(const vierkant::WindowPtr &w);

    void create_context_and_window();

    void create_graphics_pipeline();

    void create_ui();

    void create_texture_image();

    void load_model(const std::string &path = "");

    void load_environment(const std::string &path);

    void create_offscreen_assets();

    bool m_use_msaa = true;

    bool m_fullscreen = false;

    bool m_draw_grid = true;

    bool m_draw_aabb = true;

    float m_cam_distance = 20.f;

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    vk::PerspectiveCameraPtr m_camera;

    vk::Arcball m_arcball;

    vk::MeshPtr m_selected_mesh = vk::Mesh::create();

    vk::MeshPtr m_skybox = nullptr;

    std::vector<vierkant::Framebuffer> m_framebuffers_offscreen;

    vk::PipelineCachePtr m_pipeline_cache;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    vierkant::SceneRendererPtr m_scene_renderer;

    vk::Renderer m_renderer, m_renderer_gui, m_renderer_offscreen;

    vierkant::FontPtr m_font;

    vierkant::gui::Context m_gui_context;

    vierkant::DrawContext m_draw_context;

};

int main(int argc, char *argv[])
{
    auto app = std::make_shared<Vierkant3DViewer>(argc, argv);
    return app->run();
}
