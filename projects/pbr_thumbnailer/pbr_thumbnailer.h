//  MIT License
//
//  Copyright (c) 2023 Fabian Schmidt (github.com/crocdialer)
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.

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
        //! desired log-level
        spdlog::level::level_enum log_level = spdlog::level::off;

        //! path to an input model-file (.gltf | .glb)
        std::filesystem::path model_path;

        //! output-image path
        std::filesystem::path result_image_path;

        //! output-image resolution
        glm::uvec2 result_image_size = {1024, 1024};

        //! azimuth- and polar-angles for camera-placement in radians
        glm::vec2 cam_spherical_coords = {1.1f, -0.5f};

        //! flag to request a path-tracer rendering-backend
        bool use_pathtracer = false;

        //! flag to request drawing of used skybox
        bool draw_skybox = false;

        //! flag to enable vulkan validation-layers
        bool use_validation = false;
    };

    explicit PBRThumbnailer(const crocore::Application::create_info_t &create_info, settings_t settings)
        : crocore::Application(create_info), m_settings(std::move(settings)){};

    bool load_model_file(const std::filesystem::path &path);

private:
    void setup() override;

    void update(double time_delta) override;

    void teardown() override;

    void poll_events() override{};

    void create_graphics_pipeline();

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

    settings_t m_settings;
};

#endif//VIERKANT_PROJECTS_PBR_THUMBNAILER_H
