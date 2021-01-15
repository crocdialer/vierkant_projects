#include <vierkant/imgui/imgui_util.h>

#include "vierkant_projects/simple_raytracing_shaders.hpp"

#include "simple_raytrace.hpp"

void SimpleRayTracing::setup()
{
    crocore::g_logger.set_severity(crocore::Severity::DEBUG);

    create_context_and_window();
    load_model();
    create_graphics_pipeline();
}

void SimpleRayTracing::teardown()
{
    LOG_INFO << "ciao " << name();
    vkDeviceWaitIdle(m_device->handle());
}

void SimpleRayTracing::poll_events()
{
    glfwPollEvents();
}

void SimpleRayTracing::create_context_and_window()
{
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::get_required_extensions());

    vierkant::Window::create_info_t window_info = {};
    window_info.instance = m_instance.handle();
    window_info.size = {WIDTH, HEIGHT};
    window_info.title = name();
    window_info.fullscreen = m_fullscreen;
    m_window = vk::Window::create(window_info);

    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {};
    acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
//    acceleration_structure_features.accelerationStructure = true;
//    acceleration_structure_features.accelerationStructureHostCommands = true;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features = {};
    ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
//    ray_tracing_pipeline_features.rayTracingPipeline = true;
//    ray_tracing_pipeline_features.rayTracingPipelineTraceRaysIndirect = true;
//    ray_tracing_pipeline_features.rayTraversalPrimitiveCulling = true;

    VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {};
    ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
//    ray_query_features.rayQuery = true;

    // create device
    vk::Device::create_info_t device_info = {};
    device_info.instance = m_instance.handle();
    device_info.physical_device = m_instance.physical_devices().front();
    device_info.use_validation = m_instance.use_validation_layers();
    device_info.enable_device_address = true;
    device_info.surface = m_window->surface();

    // create pNext-chain
    device_info.create_device_pNext = &acceleration_structure_features;
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    ray_tracing_pipeline_features.pNext = &ray_query_features;

    // query available features for the raytracing-extensions
    VkPhysicalDeviceFeatures2 device_features = {};
    device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    device_features.pNext = &acceleration_structure_features;
    vkGetPhysicalDeviceFeatures2(device_info.physical_device, &device_features);

    // add the raytracing-extension
    device_info.extensions = {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                              VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
                              VK_KHR_RAY_QUERY_EXTENSION_NAME,
                              VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
                              VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};

    // VK_KHR_MAINTENANCE3_EXTENSION_NAME (vulkan 1.1 core)
    // VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME (vulkan 1.2 core)

    m_device = vk::Device::create(device_info);

    // create our raytracing-thingy
    m_ray_tracer = vierkant::RaytracingPipeline(m_device);

    m_window->create_swapchain(m_device, m_use_msaa ? m_device->max_usable_samples() : VK_SAMPLE_COUNT_1_BIT, V_SYNC);

    // create a WindowDelegate
    vierkant::window_delegate_t window_delegate = {};
    window_delegate.draw_fn = [this](const vierkant::WindowPtr &w){ return draw(w); };
    window_delegate.resize_fn = [this](uint32_t w, uint32_t h)
    {
        create_graphics_pipeline();
        m_camera->set_aspect(m_window->aspect_ratio());
    };
    window_delegate.close_fn = [this](){ set_running(false); };
    m_window->window_delegates[name()] = window_delegate;

    // create a KeyDelegate
    vierkant::key_delegate_t key_delegate = {};
    key_delegate.key_press = [this](const vierkant::KeyEvent &e)
    {
        if(!(m_gui_context.capture_flags() & vk::gui::Context::WantCaptureKeyboard))
        {
            if(e.code() == vk::Key::_ESCAPE){ set_running(false); }
        }
    };
    m_window->key_delegates["main"] = key_delegate;

    // create a gui and add a draw-delegate
    m_gui_context = vk::gui::Context(m_device);
    m_gui_context.delegates["application"] = [this]
    {
        vk::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
    };

    // attach gui input-delegates to window
    m_window->key_delegates["gui"] = m_gui_context.key_delegate();
    m_window->mouse_delegates["gui"] = m_gui_context.mouse_delegate();

    // camera
    m_camera = vk::PerspectiveCamera::create(m_window->aspect_ratio(), 45.f, .1f, 100.f);
    m_camera->set_position(glm::vec3(0.f, 0.f, 2.f));
}

void SimpleRayTracing::create_graphics_pipeline()
{
    const auto &framebuffers = m_window->swapchain().framebuffers();
    auto fb_extent = framebuffers.front().extent();

    vierkant::Renderer::create_info_t create_info = {};
    create_info.num_frames_in_flight = framebuffers.size();
    create_info.sample_count = m_window->swapchain().sample_count();
    create_info.viewport = {0.f, 0.f, static_cast<float>(fb_extent.width),
                            static_cast<float>(fb_extent.height), 0.f,
                            static_cast<float>(fb_extent.depth)};

    m_renderer = vierkant::Renderer(m_device, create_info);
    m_gui_renderer = vierkant::Renderer(m_device, create_info);
}

void SimpleRayTracing::load_model()
{
    auto geom = vk::Geometry::create();
    geom->vertices = {glm::vec3(-0.5f, -0.5f, 0.f),
                      glm::vec3(0.5f, -0.5f, 0.f),
                      glm::vec3(0.f, 0.5f, 0.f)};
    geom->colors = {glm::vec4(1.f, 0.f, 0.f, 1.f),
                    glm::vec4(0.f, 1.f, 0.f, 1.f),
                    glm::vec4(0.f, 0.f, 1.f, 1.f)};

    geom->indices = {0, 1, 2};

    vierkant::Mesh::create_info_t mesh_create_info = {};
    mesh_create_info.buffer_usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    m_mesh = vk::Mesh::create_from_geometry(m_device, geom, mesh_create_info);

    m_drawable = vk::Renderer::create_drawables(m_mesh).front();
    m_drawable.pipeline_format.shader_stages = vierkant::create_shader_stages(m_device,
                                                                              vierkant::ShaderType::UNLIT_COLOR);

    m_ray_tracer.add_mesh(m_mesh);
}

void SimpleRayTracing::update(double time_delta)
{
    m_drawable.matrices.modelview = m_camera->view_matrix();
    m_drawable.matrices.projection = m_camera->projection_matrix();

    // issue top-level draw-command
    m_window->draw();
}

std::vector<VkCommandBuffer> SimpleRayTracing::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = m_window->swapchain().framebuffers()[image_index];

    auto render_mesh = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_renderer.stage_drawable(m_drawable);
        return m_renderer.render(framebuffer);
    };

    auto render_gui = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_gui_renderer);
        return m_gui_renderer.render(framebuffer);
    };

    bool concurrant_draw = true;

    if(concurrant_draw)
    {
        // submit and wait for all command-creation tasks to complete
        std::vector<std::future<VkCommandBuffer>> cmd_futures;
        cmd_futures.push_back(background_queue().post(render_mesh));
        cmd_futures.push_back(background_queue().post(render_gui));
        crocore::wait_all(cmd_futures);

        // get values from completed futures
        std::vector<VkCommandBuffer> command_buffers;
        for(auto &f : cmd_futures){ command_buffers.push_back(f.get()); }
        return command_buffers;
    }
    return {render_mesh(), render_gui()};
}
