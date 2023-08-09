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

#include <boost/program_options.hpp>

#include <vierkant/CameraControl.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/gltf.hpp>

#include "pbr_thumbnailer.h"

using double_second = std::chrono::duration<double>;

void PBRThumbnailer::setup()
{
    spdlog::set_level(settings.log_level);

    auto load_future = background_queue().post([this]() -> bool { return load_model_file(settings.model_path); });

    // create required vulkan-resources
    create_graphics_pipeline();

    // load model
    this->running = load_future.get();
}

void PBRThumbnailer::update(double /*time_delta*/)
{
    // render image
    {
        spdlog::stopwatch sw;

        uint32_t num_passes = settings.use_pathtracer ? 128 : 1;

        for(uint32_t i = 0; i < num_passes; ++i)
        {
            auto render_result = m_scene_renderer->render_scene(m_renderer, m_scene, m_camera, {});
            auto cmd_buffer = m_renderer.render(m_framebuffer);
            m_framebuffer.submit({cmd_buffer}, m_device->queue(), render_result.semaphore_infos);
            m_framebuffer.wait_fence();
        }

        // if 'done' -> terminate loop
        spdlog::info("rendering done (#passes: {} - {})", num_passes, sw.elapsed());
        this->running = false;
    }

    {
        spdlog::stopwatch sw;

        // download result image from GPU
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
        crocore::save_image_to_file(result_img, settings.result_image_path.string());
        spdlog::info("png/jpg encoding ({})", sw.elapsed());
    }
}

void PBRThumbnailer::teardown()
{
    vkDeviceWaitIdle(m_device->handle());
    spdlog::info("total: {}s", application_time());
}

bool PBRThumbnailer::load_model_file(const std::filesystem::path &path)
{
    if(exists(path))
    {
        auto start_time = std::chrono::steady_clock::now();

        spdlog::debug("loading model '{}'", path.string());

        // tinygltf
        auto scene_assets = vierkant::model::gltf(path);
        spdlog::info("loaded model: '{}' ({})", path.string(),
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
        load_params.mesh_buffers_params.pack_vertices = true;
        auto mesh = vierkant::model::load_mesh(load_params, scene_assets);

        // attach mesh to an object, insert into scene
        {
            auto object = vierkant::create_mesh_object(m_scene->registry(), {mesh});
            object->name = std::filesystem::path(path).filename().string();

            // scale
            object->transform.scale = glm::vec3(1.f / glm::length(object->aabb().half_extents()));

            // center aabb
            auto aabb = object->aabb().transform(vierkant::mat4_cast(object->transform));
            object->transform.translation = -aabb.center();//+ glm::vec3(0.f, aabb.height() / 2.f, 0.f);

            m_scene->add_object(object);
        }
        return true;
    }
    spdlog::error("could not find file: '{}'", path.string());
    return false;
}

void PBRThumbnailer::create_graphics_pipeline()
{
    spdlog::stopwatch sw;

    vierkant::Instance::create_info_t instance_info = {};
    instance_info.use_validation_layers = settings.use_validation;
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

    // setup a scene-renderer
    if(settings.use_pathtracer)
    {
        vierkant::PBRPathTracer::create_info_t path_tracer_info = {};
        path_tracer_info.settings.compaction = false;
        path_tracer_info.settings.resolution = settings.result_image_size;
        path_tracer_info.settings.max_num_batches = 256;
        path_tracer_info.settings.num_samples = 8;
        path_tracer_info.settings.draw_skybox = settings.draw_skybox;
        m_scene_renderer = vierkant::PBRPathTracer::create(m_device, path_tracer_info);
    }
    else
    {
        vierkant::PBRDeferred::create_info_t pbr_render_info = {};
        pbr_render_info.settings.resolution = settings.result_image_size;
        pbr_render_info.settings.output_resolution = settings.result_image_size;
        pbr_render_info.settings.draw_skybox = settings.draw_skybox;
        pbr_render_info.settings.indirect_draw = false;
        pbr_render_info.settings.use_taa = false;
        pbr_render_info.settings.use_fxaa = true;
        m_scene_renderer = vierkant::PBRDeferred::create(m_device, pbr_render_info);
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

    // set camera-position
    auto orbit_cam_controller = vierkant::OrbitCamera();
    orbit_cam_controller.spherical_coords = settings.cam_spherical_coords;
    orbit_cam_controller.distance = 2.5f;
    m_camera->transform = orbit_cam_controller.transform();

    // create framebuffer
    vierkant::Framebuffer::create_info_t framebuffer_info = {};
    framebuffer_info.size = {settings.result_image_size.x, settings.result_image_size.y, 1};
    framebuffer_info.color_attachment_format.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    m_framebuffer = vierkant::Framebuffer(m_device, framebuffer_info);

    spdlog::info("graphics-pipeline initialized: {}", sw.elapsed());
}

std::optional<PBRThumbnailer::settings_t> parse_settings(char **argv, int argc)
{
    namespace po = boost::program_options;
    PBRThumbnailer::settings_t ret = {};

    bool success = false;

    // Declare the supported options.
    po::options_description desc("Available options");
    desc.add_options()("help", "produce help message");
    desc.add_options()("width,w", po::value<uint32_t>(), "set result-image width in px");
    desc.add_options()("height,h", po::value<uint32_t>(), "set result-image height in px");
    desc.add_options()("angle,a", po::value<float>(), "set camera rotation-angle in degrees");
    desc.add_options()("skybox,s", po::bool_switch(), "render skybox");
    desc.add_options()("pathtracer,p", po::bool_switch(), "use pathtracing");
    desc.add_options()("verbose,v", po::bool_switch(), "verbose printing");
    desc.add_options()("validation", po::bool_switch(), "enable vulkan validation");
    desc.add_options()("input-file", po::value<std::vector<std::string>>(), "input file");

    po::positional_options_description p;
    p.add("input-file", -1);

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    po::notify(vm);

    if(vm.count("input-file"))
    {
        const auto &files = vm["input-file"].as<std::vector<std::string>>();

        if(files.size() == 2)
        {
            ret.model_path = files[0];
            ret.result_image_path = files[1];
            success = true;
        }
    }

    // print usage
    if(!success || vm.count("help"))
    {
        spdlog::info("usage: pbr_thumbnailer [options...] <model_path> <result_image_path>");
        std::stringstream ss;
        ss << desc;
        spdlog::set_pattern("%v");
        spdlog::info("\n{}",ss.str());
        return {};
    }
    if(vm.count("width")) { ret.result_image_size.x = vm["width"].as<uint32_t>(); }
    if(vm.count("height")) { ret.result_image_size.y = vm["height"].as<uint32_t>(); }
    if(vm.count("angle")) { ret.cam_spherical_coords.x = glm::radians(vm["angle"].as<float>()); }
    if(vm.count("skybox") && vm["skybox"].as<bool>()) { ret.draw_skybox = true; }
    if(vm.count("pathtracer") && vm["pathtracer"].as<bool>()) { ret.use_pathtracer = true; }
    if(vm.count("validation") && vm["validation"].as<bool>()) { ret.use_validation = true; }
    if(vm.count("verbose") && vm["verbose"].as<bool>()) { ret.log_level = spdlog::level::info; }
    return ret;
}

int main(int argc, char *argv[])
{
    crocore::Application::create_info_t create_info = {};
    create_info.arguments = {argv, argv + argc};
    create_info.num_background_threads = 1;

    if(auto settings = parse_settings(argv, argc))
    {
        auto app = std::make_shared<PBRThumbnailer>(create_info);
        app->settings = *settings;
        return app->run();
    }
    else { return EXIT_FAILURE; }
}
