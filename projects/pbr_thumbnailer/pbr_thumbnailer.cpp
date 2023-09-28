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
#include <vierkant/cubemap_utils.hpp>
#include <vierkant/model/model_loading.hpp>

#include "pbr_thumbnailer.h"

using double_second = std::chrono::duration<double>;

void PBRThumbnailer::setup()
{
    spdlog::set_level(m_settings.log_level);

    // print vulkan/driver/vierkant-version
    spdlog::info("{}: processing model '{}' -> '{}'", name(), m_settings.model_path.string(),
                 m_settings.result_image_path.string());

    // load model in background
    auto scene_future = background_queue().post(
            [path = m_settings.model_path, &pool = background_queue()] { return load_model_file(path, pool); });

    // TODO: load optional environment-HDR

    // create required vulkan-resources
    create_graphics_context();

    auto scene_data = scene_future.get();

    // create camera
    if(scene_data) { create_camera(*scene_data); }

    // load model
    bool success = scene_data && create_mesh(*scene_data);
    this->running = success;
    if(!success) { return_type = EXIT_FAILURE; }
}

void PBRThumbnailer::update(double /*time_delta*/)
{
    // render image
    {
        spdlog::stopwatch sw;

        uint32_t num_passes = m_settings.num_samples / m_settings.max_samples_per_frame;

        for(uint32_t i = 0; i < num_passes; ++i)
        {
            auto render_result = m_context.scene_renderer->render_scene(m_context.renderer, m_scene, m_camera, {});
            auto cmd_buffer = m_context.renderer.render(m_context.framebuffer);
            m_context.framebuffer.submit({cmd_buffer}, m_context.device->queue(), render_result.semaphore_infos);
            m_context.framebuffer.wait_fence();
        }
        spdlog::info("rendering done (#spp: {} - {})", m_settings.num_samples, sw.elapsed());
    }

    {
        spdlog::stopwatch sw;

        // download result image from GPU
        vierkant::Buffer::create_info_t host_buffer_info = {};
        host_buffer_info.device = m_context.device;
        host_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        host_buffer_info.mem_usage = VMA_MEMORY_USAGE_CPU_ONLY;
        host_buffer_info.num_bytes = vierkant::num_bytes(m_context.framebuffer.color_attachment()->format().format) *
                                     m_settings.result_image_size.x * m_settings.result_image_size.y;
        auto host_buffer = vierkant::Buffer::create(host_buffer_info);
        m_context.framebuffer.color_attachment()->copy_to(host_buffer);

        // save image to disk
        auto result_img = crocore::Image_<uint8_t>::create(static_cast<uint8_t *>(host_buffer->map()),
                                                           m_settings.result_image_size.x,
                                                           m_settings.result_image_size.y, 4, true);
        crocore::save_image_to_file(result_img, m_settings.result_image_path.string());
        spdlog::info("png/jpg encoding ({})", sw.elapsed());
    }

    // done -> terminate application-loop
    this->running = false;
}

void PBRThumbnailer::teardown()
{
    vkDeviceWaitIdle(m_context.device->handle());
    spdlog::info("total: {}s", application_time());
}

std::optional<vierkant::model::mesh_assets_t> PBRThumbnailer::load_model_file(const std::filesystem::path &path,
                                                                              crocore::ThreadPool &pool)
{
    if(exists(path))
    {
        auto start_time = std::chrono::steady_clock::now();

        spdlog::debug("loading model '{}'", path.string());

        // tinygltf
        auto scene_assets = vierkant::model::load_model(path, &pool);

        if(!scene_assets || scene_assets->entry_create_infos.empty())
        {
            spdlog::error("could not load file: {}", path.string());
            return {};
        }
        spdlog::info("loaded model: '{}' ({})", path.string(),
                     double_second(std::chrono::steady_clock::now() - start_time));
        return scene_assets;
    }
    spdlog::error("could not find file: '{}'", path.string());
    return {};
}

bool PBRThumbnailer::create_graphics_context()
{
    spdlog::stopwatch sw;

    vierkant::Instance::create_info_t instance_info = {};
    instance_info.use_validation_layers = m_settings.use_validation;
    m_context.instance = vierkant::Instance(instance_info);

    VkPhysicalDevice physical_device = m_context.instance.physical_devices().front();

    for(const auto &pd: m_context.instance.physical_devices())
    {
        VkPhysicalDeviceProperties device_props = {};
        vkGetPhysicalDeviceProperties(pd, &device_props);

        if(device_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            physical_device = pd;
            break;
        }
    }
    // print vulkan-/driver-/vierkant-version
    spdlog::debug(vierkant::device_info(physical_device));

    // check raytracing-pipeline support
    m_settings.use_pathtracer =
            m_settings.use_pathtracer &&
            vierkant::check_device_extension_support(physical_device, vierkant::RayTracer::required_extensions());

    // create device
    vierkant::Device::create_info_t device_info = {};
    device_info.use_validation = m_context.instance.use_validation_layers();
    device_info.instance = m_context.instance.handle();
    device_info.physical_device = physical_device;
    device_info.direct_function_pointers = true;

    // add the raytracing-extensions
    if(m_settings.use_pathtracer)
    {
        device_info.extensions = crocore::concat_containers<const char *>(vierkant::RayTracer::required_extensions(),
                                                                          vierkant::RayBuilder::required_extensions());
        if(!vierkant::check_device_extension_support(physical_device, device_info.extensions))
        {
            spdlog::warn("using fallback rasterizer: path-tracer was requested, but required extensions are not "
                         "available {}",
                         device_info.extensions);
        }
    }
    m_context.device = vierkant::Device::create(device_info);

    // setup a scene-renderer
    if(m_settings.use_pathtracer)
    {
        vierkant::PBRPathTracer::create_info_t path_tracer_info = {};
        path_tracer_info.settings.compaction = false;
        path_tracer_info.settings.resolution = m_settings.result_image_size;
        path_tracer_info.settings.max_num_batches = m_settings.num_samples / m_settings.max_samples_per_frame;
        path_tracer_info.settings.num_samples = m_settings.max_samples_per_frame;
        path_tracer_info.settings.draw_skybox = m_settings.draw_skybox;
        m_context.scene_renderer = vierkant::PBRPathTracer::create(m_context.device, path_tracer_info);
    }
    else
    {
        constexpr auto hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;// VK_FORMAT_B10G11R11_UFLOAT_PACK32

        vierkant::PBRDeferred::create_info_t pbr_render_info = {};
        pbr_render_info.settings.resolution = m_settings.result_image_size;
        pbr_render_info.settings.output_resolution = m_settings.result_image_size;
        pbr_render_info.settings.draw_skybox = m_settings.draw_skybox;
        pbr_render_info.settings.indirect_draw = false;
        pbr_render_info.settings.use_taa = false;
        pbr_render_info.settings.use_fxaa = true;
        pbr_render_info.hdr_format = hdr_format;

        constexpr uint32_t env_size = 256;
        constexpr uint32_t lambert_size = 64;
        auto env_img = vierkant::cubemap_neutral_environment(m_context.device, env_size, m_context.device->queue(),
                                                             true, hdr_format);
        pbr_render_info.conv_lambert = vierkant::create_convolution_lambert(m_context.device, env_img, lambert_size,
                                                                            hdr_format, m_context.device->queue());
        pbr_render_info.conv_ggx = vierkant::create_convolution_ggx(m_context.device, env_img, env_img->width(),
                                                                    hdr_format, m_context.device->queue());
        m_context.scene_renderer = vierkant::PBRDeferred::create(m_context.device, pbr_render_info);
    }
    vierkant::Rasterizer::create_info_t create_info = {};
    create_info.viewport.width = static_cast<float>(m_settings.result_image_size.x);
    create_info.viewport.height = static_cast<float>(m_settings.result_image_size.y);
    m_context.renderer = vierkant::Rasterizer(m_context.device, create_info);

    // create framebuffer
    vierkant::Framebuffer::create_info_t framebuffer_info = {};
    framebuffer_info.size = {m_settings.result_image_size.x, m_settings.result_image_size.y, 1};
    framebuffer_info.color_attachment_format.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    m_context.framebuffer = vierkant::Framebuffer(m_context.device, framebuffer_info);

    // clear with transparent alpha, if requested
    if(!m_settings.draw_skybox) { m_context.framebuffer.clear_color = {{0.f, 0.f, 0.f, 0.f}}; }

    spdlog::debug("graphics-context initialized: {}", sw.elapsed());
    return true;
}

bool PBRThumbnailer::create_mesh(const vierkant::model::mesh_assets_t &mesh_assets)
{
    // additionally required buffer-flags for raytracing/compute/mesh-shading
    VkBufferUsageFlags buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    if(m_settings.use_pathtracer)
    {
        buffer_flags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    }

    // create a gpu-mesh from loaded assets
    vierkant::model::load_mesh_params_t load_params = {};
    load_params.device = m_context.device;
    load_params.buffer_flags = buffer_flags;
    load_params.mesh_buffers_params.pack_vertices = true;
    auto mesh = vierkant::model::load_mesh(load_params, mesh_assets);

    // attach mesh to an object, insert into scene
    {
        auto object = vierkant::create_mesh_object(m_scene->registry(), {mesh});

        // scale
        object->transform.scale = glm::vec3(1.f / glm::length(object->aabb().half_extents()));

        // center aabb
        auto aabb = object->aabb().transform(vierkant::mat4_cast(object->transform));
        object->transform.translation = -aabb.center();

        m_scene->add_object(object);
    }
    return true;
}
void PBRThumbnailer::create_camera(const vierkant::model::mesh_assets_t &mesh_assets)
{
    vierkant::model::camera_t model_camera = {};

    // prefer/expose cameras included in model-files
    if(m_settings.use_model_camera && !mesh_assets.cameras.empty()) { model_camera = mesh_assets.cameras.front(); }
    else
    {
        // set camera-position
        auto orbit_cam_controller = vierkant::OrbitCamera();
        orbit_cam_controller.spherical_coords = m_settings.cam_spherical_coords;
        orbit_cam_controller.distance = 2.5f;
        model_camera.transform = orbit_cam_controller.transform();
    }

    // create camera and add to scene
    model_camera.params.aspect =
            static_cast<float>(m_settings.result_image_size.x) / static_cast<float>(m_settings.result_image_size.y);

    m_camera = vierkant::PerspectiveCamera::create(m_scene->registry(), model_camera.params);
    m_camera->transform = model_camera.transform;
}

std::optional<PBRThumbnailer::settings_t> parse_settings(int argc, char *argv[])
{
    namespace po = boost::program_options;
    PBRThumbnailer::settings_t ret = {};

    bool success = true;

    // available options
    po::options_description desc("Available options");
    desc.add_options()("help", "produce help message");
    desc.add_options()("width,w", po::value<uint32_t>(), "set result-image width in px");
    desc.add_options()("height,h", po::value<uint32_t>(), "set result-image height in px");
    desc.add_options()("angle,a", po::value<float>(), "set camera rotation-angle in degrees");
    desc.add_options()("skybox,s", po::bool_switch(), "render skybox");
    desc.add_options()("camera,c", po::bool_switch(), "prefer model-camera");
    desc.add_options()("raster,r", po::bool_switch(), "force fallback-rasterizer instead of path-tracing");
    desc.add_options()("verbose,v", po::bool_switch(), "verbose printing");
    desc.add_options()("validation", po::bool_switch(), "enable vulkan validation");
    desc.add_options()("input-file", po::value<std::vector<std::string>>(), "input file");

    po::positional_options_description p;
    p.add("input-file", -1);

    po::variables_map vm;

    try
    {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
        po::notify(vm);
    } catch(std::exception &e)
    {
        success = false;
        spdlog::error(e.what());
    }

    if(vm.count("input-file"))
    {
        const auto &files = vm["input-file"].as<std::vector<std::string>>();

        for(const auto &f: files)
        {
            auto file_path = std::filesystem::path(f);
            auto ext = crocore::to_lower(file_path.extension().string());
            bool file_exists = exists(file_path) && is_regular_file(file_path);

            if(file_exists && (ext == ".gltf" || ext == ".glb" || ext == ".obj")) { ret.model_path = file_path; }
            else if(file_exists && (ext == ".hdr")) { ret.environment_path = file_path; }
            else if(ext == ".png") { ret.result_image_path = file_path; }
        }
    }
    if(ret.model_path.empty()) { spdlog::error("no valid model-file (.gltf | .glb | .obj)"); }
    if(ret.result_image_path.empty()) { spdlog::error("no valid output-image path (.png | .jpg)"); }

    success = success && !ret.model_path.empty() && !ret.result_image_path.empty();

    // print usage
    if(!success || vm.count("help"))
    {
        spdlog::info("usage: pbr_thumbnailer [options...] <model_path> <result_image_path>");
        spdlog::set_pattern("%v");
        spdlog::info("\n{}", (std::stringstream() << desc).str());
        return {};
    }
    if(vm.count("width")) { ret.result_image_size.x = vm["width"].as<uint32_t>(); }
    if(vm.count("height")) { ret.result_image_size.y = vm["height"].as<uint32_t>(); }
    if(vm.count("angle")) { ret.cam_spherical_coords.x = glm::radians(vm["angle"].as<float>()); }
    if(vm.count("skybox") && vm["skybox"].as<bool>()) { ret.draw_skybox = true; }
    if(vm.count("camera") && vm["camera"].as<bool>()) { ret.use_model_camera = true; }
    if(vm.count("raster") && vm["raster"].as<bool>()) { ret.use_pathtracer = false; }
    if(vm.count("validation") && vm["validation"].as<bool>()) { ret.use_validation = true; }
    if(vm.count("verbose") && vm["verbose"].as<bool>()) { ret.log_level = spdlog::level::info; }
    return ret;
}

int main(int argc, char *argv[])
{
    if(auto settings = parse_settings(argc, argv))
    {
        crocore::Application::create_info_t create_info = {};
        create_info.arguments = {argv, argv + argc};
        create_info.num_background_threads = std::min<uint32_t>(4, std::thread::hardware_concurrency());
        auto app = PBRThumbnailer(create_info, *settings);
        return app.run();
    }
    else { return EXIT_FAILURE; }
}
