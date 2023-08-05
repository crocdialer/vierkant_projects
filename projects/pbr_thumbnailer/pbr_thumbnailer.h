//
// Created by crocdialer on 05.08.23.
//

#ifndef VIERKANT_PROJECTS_PBR_THUMBNAILER_H
#define VIERKANT_PROJECTS_PBR_THUMBNAILER_H

#include <crocore/Application.hpp>

#include <filesystem>
#include <vierkant/Scene.hpp>
#include <vierkant/SceneRenderer.hpp>

class PBRThumbnailer : public crocore::Application
{
public:
    struct settings_t
    {
        spdlog::level::level_enum log_level = spdlog::level::info;

        std::filesystem::path model_path;

        std::filesystem::path result_image_path;

        glm::uvec2 result_image_size = {512, 512};

        bool use_pathtracer = true;
    };

    explicit PBRThumbnailer(const crocore::Application::create_info_t &create_info)
        : crocore::Application(create_info){};

    bool load_model_file(const std::filesystem::path &path);

    settings_t settings;

private:
    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override{};

    void print_usage();

    // instance
    vierkant::Instance m_instance;

    // device
    vierkant::DevicePtr m_device;

    // output rasterizer
    vierkant::Renderer m_renderer;
    vierkant::Framebuffer m_framebuffer;

    vierkant::ScenePtr m_scene = vierkant::Scene::create();

    vierkant::SceneRendererPtr m_scene_renderer = nullptr;

    vierkant::CameraPtr m_camera;
};

#endif//VIERKANT_PROJECTS_PBR_THUMBNAILER_H
