//
// Created by crocdialer on 05.08.23.
//

#include <vierkant/CameraControl.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/gltf.hpp>

#include "pbr_thumbnailer.h"

using double_second = std::chrono::duration<double>;

void PBRThumbnailer::setup()
{
    settings.log_level = spdlog::level::debug;
    bool use_validation = true;

    spdlog::set_level(settings.log_level);

    if(args().size() > 2)
    {
        settings.model_path = args()[1];
        settings.result_image_path = args()[2];
    }
    else
    {
        print_usage();
        this->running = false;
        return;
    }

    vierkant::Instance::create_info_t instance_info = {};
    instance_info.use_validation_layers = use_validation;
    m_instance = vierkant::Instance(instance_info);

    VkPhysicalDevice physical_device = m_instance.physical_devices().front();

    for(const auto &pd: m_instance.physical_devices())
    {
        VkPhysicalDeviceProperties device_props = {};
        vkGetPhysicalDeviceProperties(pd, &device_props);

        if(device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physical_device = pd;
            break;
        }
    }

    // check raytracing-pipeline support
    settings.use_pathtracer =
            settings.use_pathtracer &&
            vierkant::check_device_extension_support(physical_device, vierkant::RayTracer::required_extensions());

    // create device
    vierkant::Device::create_info_t device_info = {};
    device_info.use_validation = m_instance.use_validation_layers();
    device_info.instance = m_instance.handle();
    device_info.physical_device = physical_device;

    // TODO: warn if path-tracer was requested but is not available

    // add the raytracing-extensions
    if(settings.use_pathtracer)
    {
        device_info.extensions = crocore::concat_containers<const char *>(vierkant::RayTracer::required_extensions(),
                                                                          vierkant::RayBuilder::required_extensions());
    }

    m_device = vierkant::Device::create(device_info);

    // TODO: setup renderer
    if(settings.use_pathtracer)
    {
        vierkant::PBRPathTracer::create_info_t path_tracer_info = {};
        path_tracer_info.num_frames_in_flight = 1;
        path_tracer_info.settings.compaction = false;
        path_tracer_info.settings.max_num_batches = 256;
        path_tracer_info.settings.num_samples = 8;
        path_tracer_info.settings.draw_skybox = false;
        m_scene_renderer = vierkant::PBRPathTracer::create(m_device, path_tracer_info);
    }

    vierkant::Renderer::create_info_t create_info = {};
    create_info.num_frames_in_flight = 1;
    create_info.viewport.width = static_cast<float>(settings.result_image_size.x);
    create_info.viewport.height = static_cast<float>(settings.result_image_size.y);
    m_renderer = vierkant::Renderer(m_device, create_info);

    // create camera and add to scene (TODO: prefer/expose cameras included in model-files)
    vierkant::physical_camera_params_t camera_params = {};
    camera_params.aspect =
            static_cast<float>(settings.result_image_size.x) / static_cast<float>(settings.result_image_size.y);
    m_camera = vierkant::PerspectiveCamera::create(m_scene->registry(), camera_params);
    m_scene->add_object(m_camera);

    // set camera-position
    auto orbit_cam_controller = vierkant::OrbitCamera();
//    orbit_cam_controller.spherical_coords = {0.5414924621582031, -0.4363316595554352};
    orbit_cam_controller.distance = 15.f;
    m_camera->transform = orbit_cam_controller.transform();

    // create framebuffer
    vierkant::Framebuffer::create_info_t framebuffer_info = {};
    framebuffer_info.size = {settings.result_image_size.x, settings.result_image_size.y, 1};
    framebuffer_info.color_attachment_format.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    m_framebuffer = vierkant::Framebuffer(m_device, framebuffer_info);

    // load model
    this->running = load_model_file(settings.model_path);
}

void PBRThumbnailer::update(double /*time_delta*/)
{
    // TODO: render image
    {
        spdlog::stopwatch sw;
        spdlog::debug("render image");

        for(uint32_t i = 0; i < 5; ++i)
        {
            m_framebuffer.wait_fence();
            m_scene_renderer->render_scene(m_renderer, m_scene, m_camera, {});
            auto cmd_buffer = m_renderer.render(m_framebuffer);
            m_framebuffer.submit({cmd_buffer}, m_device->queue());
        }

        // TODO: if 'done' -> terminate loop
        spdlog::debug("done ({})", sw.elapsed());
        this->running = false;
    }

    {
        spdlog::stopwatch sw;
        spdlog::debug("saving image: {}", settings.result_image_path.string());
        // TODO: download result image from GPU
        vierkant::Buffer::create_info_t host_buffer_info = {};
        host_buffer_info.device = m_device;
        host_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        host_buffer_info.mem_usage = VMA_MEMORY_USAGE_CPU_ONLY;
        host_buffer_info.num_bytes = vierkant::num_bytes(m_framebuffer.color_attachment()->format().format) *
                                     settings.result_image_size.x * settings.result_image_size.y;
        auto host_buffer = vierkant::Buffer::create(host_buffer_info);
        m_framebuffer.color_attachment()->copy_to(host_buffer);

        // save image to disk
        auto result_img =
                crocore::Image_<uint8_t>::create(static_cast<uint8_t *>(host_buffer->map()),
                                                 settings.result_image_size.x, settings.result_image_size.y, 4, true);
        crocore::save_image_to_file(result_img, settings.result_image_path);
        spdlog::debug("done ({})", sw.elapsed());
    }
}

void PBRThumbnailer::teardown()
{
    // TODO: report
    vkDeviceWaitIdle(m_device->handle());
    spdlog::info("processing took: {} ms", 1000.0 * application_time());
}

bool PBRThumbnailer::load_model_file(const std::filesystem::path &path)
{
    if(exists(path))
    {
        auto start_time = std::chrono::steady_clock::now();

        spdlog::debug("loading model '{}'", path.string());

        // tinygltf
        auto scene_assets = vierkant::model::gltf(path);
        spdlog::debug("loaded model '{}' ({})", path.string(),
                      double_second(std::chrono::steady_clock::now() - start_time));

        if(scene_assets.entry_create_infos.empty())
        {
            spdlog::warn("could not load file: {}", path.string());
            return false;
        }

        // additionally required buffer-flags for raytracing/compute/mesh-shading
        VkBufferUsageFlags buffer_flags =
                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        if(settings.use_pathtracer)
        {
            buffer_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
        }

        // create a gpu-mesh from loaded assets
        vierkant::model::load_mesh_params_t load_params = {};
        load_params.device = m_device;
        load_params.buffer_flags = buffer_flags;
        auto mesh = vierkant::model::load_mesh(load_params, scene_assets);

        // attach mesh to an object, insert into scene
        {
            auto object = vierkant::create_mesh_object(m_scene->registry(), {mesh});
            object->name = std::filesystem::path(path).filename().string();

            // scale
            object->transform.scale = glm::vec3(5.f / glm::length(object->aabb().half_extents()));

            // center aabb
            auto aabb = object->aabb().transform(vierkant::mat4_cast(object->transform));
            object->transform.translation = -aabb.center() + glm::vec3(0.f, aabb.height() / 2.f, 0.f);

            m_scene->add_object(object);
        }
        return true;
    }
    spdlog::error("invalid model-path: '{}'", path.string());
    return false;
}

void PBRThumbnailer::print_usage() { spdlog::info("usage: {} <model_path> <result_image_path>", name()); }

int main(int argc, char *argv[])
{
    crocore::Application::create_info_t create_info = {};
    create_info.arguments = {argv, argv + argc};
    create_info.num_background_threads = 4;//std::thread::hardware_concurrency();

    auto app = std::make_shared<PBRThumbnailer>(create_info);
    return app->run();
}