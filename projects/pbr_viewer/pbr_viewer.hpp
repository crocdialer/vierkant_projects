//
// Created by crocdialer on 9/1/18.
//

#pragma once

#include <filesystem>

#include <crocore/Application.hpp>
#include <vierkant/vierkant.hpp>
#include "serialization.hpp"

const int WIDTH = 1920;
const int HEIGHT = 1080;
const bool V_SYNC = true;
bool DEMO_GUI = false;

const char *g_texture_url = "http://roa.h-cdn.co/assets/cm/14/47/1024x576/546b32b33240f_-_hasselhoff_kr_pr_nbc-lg.jpg";

const char *g_font_path = "/usr/local/share/fonts/Courier New Bold.ttf";

//const char *g_font_path = "https://github.com/google/fonts/raw/master/ufl/ubuntu/Ubuntu-Medium.ttf";

VkFormat vk_format(const crocore::ImagePtr &img, bool compress = true);

void render_scene(vierkant::Renderer &renderer, vierkant::ScenePtr scene, vierkant::CameraPtr camera);

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
                        .width = 1920,
                        .height = 1080,
                        .fullscreen = false,
                        .vsync = true
                };

        vierkant::SceneRenderer::settings_t render_settings = {};

        bool draw_aabbs = true;

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

    std::vector<VkCommandBuffer> draw(const vierkant::WindowPtr &w);

    void create_context_and_window();

    void create_graphics_pipeline();

    void create_ui();

    void create_texture_image();

    void load_model(const std::string &path = "");

    void load_environment(const std::string &path);

    void create_offscreen_assets();

    void save_settings(settings_t settings, const std::filesystem::path &path = "settings.json");

    settings_t load_settings(const std::filesystem::path &path = "settings.json");

//    bool m_use_msaa = true;
//
//    bool m_fullscreen = false;
//
//    bool m_draw_aabb = true;

    settings_t m_settings;

    // bundles basic Vulkan assets
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    // window handle
    std::shared_ptr<vierkant::Window> m_window;

    std::map<std::string, vierkant::ImagePtr> m_textures;

    vk::PerspectiveCameraPtr m_camera;

    vk::Arcball m_arcball;

    std::set<vierkant::Object3DPtr> m_selected_objects;

    std::vector<vierkant::Framebuffer> m_framebuffers_offscreen;

    vk::PipelineCachePtr m_pipeline_cache;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    vierkant::PBRDeferredPtr m_pbr_renderer;

    vierkant::SceneRendererPtr m_unlit_renderer;

    vk::Renderer m_renderer, m_renderer_gui, m_renderer_offscreen;

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
       cereal::make_nvp("render_settings", settings.render_settings),
       cereal::make_nvp("draw_aabbs", settings.draw_aabbs),
       cereal::make_nvp("view_rotation", settings.view_rotation),
       cereal::make_nvp("view_look_at", settings.view_look_at),
       cereal::make_nvp("view_distance", settings.view_distance));
}

//
//} // namespace boost::serialization
