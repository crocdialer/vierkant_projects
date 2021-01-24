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
    // create instance
    m_instance = vk::Instance(g_enable_validation_layers, vk::Window::required_extensions());

    // grab first physical device
    VkPhysicalDevice physical_device = m_instance.physical_devices().front();

    vierkant::Window::create_info_t window_info = {};
    window_info.instance = m_instance.handle();
    window_info.size = {WIDTH, HEIGHT};
    window_info.title = name();
    window_info.fullscreen = m_fullscreen;
    m_window = vk::Window::create(window_info);

    // prepare extension-features query
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure_features = {};
    acceleration_structure_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline_features = {};
    ray_tracing_pipeline_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;

    VkPhysicalDeviceRayQueryFeaturesKHR ray_query_features = {};
    ray_query_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

    // create a pNext-chain connecting the extension-structures
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    ray_tracing_pipeline_features.pNext = &ray_query_features;

    // query available features for the raytracing-extensions
    VkPhysicalDeviceFeatures2 device_features = {};
    device_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    device_features.pNext = &acceleration_structure_features;
    vkGetPhysicalDeviceFeatures2(physical_device, &device_features);

    // create device
    vk::Device::create_info_t device_info = {};
    device_info.instance = m_instance.handle();
    device_info.physical_device = physical_device;
    device_info.use_validation = m_instance.use_validation_layers();
    device_info.enable_device_address = true;
    device_info.surface = m_window->surface();

    // add the raytracing-extensions
    device_info.extensions = vierkant::Raytracer::required_extensions();

    // pass populated extension-chain to enable the features
    device_info.create_device_pNext = &acceleration_structure_features;

    m_device = vk::Device::create(device_info);

    // create our raytracing-thingy
    m_ray_tracer = vierkant::Raytracer(m_device);

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
            else if(e.code() == vk::Key::_B){ m_show_ray_tracer = !m_show_ray_tracer; }
        }
    };
    m_window->key_delegates["main"] = key_delegate;

    // create a gui and add a draw-delegate
    m_gui_context = vk::gui::Context(m_device);
    m_gui_context.delegates["application"] = [this]
    {
        vk::gui::draw_application_ui(std::static_pointer_cast<Application>(shared_from_this()), m_window);
    };

    m_draw_context = vierkant::DrawContext(m_device);

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
    // simple triangle geometry
    auto geom = vk::Geometry::create();
    geom->vertices = {glm::vec3(-0.5f, -0.5f, 0.f),
                      glm::vec3(0.5f, -0.5f, 0.f),
                      glm::vec3(0.f, 0.5f, 0.f)};
    geom->colors = {glm::vec4(1.f), glm::vec4(1.f), glm::vec4(1.f)};
    geom->indices = {0, 1, 2};

    vierkant::Mesh::create_info_t mesh_create_info = {};

    // additionally required buffer-flags for raytracing
    mesh_create_info.buffer_usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    m_mesh = vk::Mesh::create_from_geometry(m_device, geom, mesh_create_info);

    m_drawable = vk::Renderer::create_drawables(m_mesh).front();
    m_drawable.pipeline_format.shader_stages = vierkant::create_shader_stages(m_device,
                                                                              vierkant::ShaderType::UNLIT_COLOR);

    m_ray_tracer.add_mesh(m_mesh);

    // create a storage image
    vierkant::Image::Format img_format = {};
    img_format.extent = {static_cast<uint32_t>(m_window->size().x), static_cast<uint32_t>(m_window->size().y), 1};
    img_format.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    img_format.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    m_storage_image = vierkant::Image::create(m_device, img_format);

    m_tracable.extent = m_storage_image->extent();

    // raygen
    m_tracable.pipeline_info.shader_stages.insert({VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                                                   vierkant::create_shader_module(m_device,
                                                                                  vierkant::shaders::simple_ray::raygen_rgen)});
    // miss
    m_tracable.pipeline_info.shader_stages.insert({VK_SHADER_STAGE_MISS_BIT_KHR,
                                                   vierkant::create_shader_module(m_device,
                                                                                  vierkant::shaders::simple_ray::miss_rmiss)});

    // closest hit
    m_tracable.pipeline_info.shader_stages.insert({VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                                                   vierkant::create_shader_module(m_device,
                                                                                  vierkant::shaders::simple_ray::closesthit_rchit)});

    // descriptors
    vierkant::descriptor_t desc_acceleration_structure = {};
    desc_acceleration_structure.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    desc_acceleration_structure.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_acceleration_structure.acceleration_structure = m_ray_tracer.acceleration_structure();
    m_tracable.descriptors[0] = desc_acceleration_structure;

    vierkant::descriptor_t desc_storage_image = {};
    desc_storage_image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    desc_storage_image.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_storage_image.image_samplers = {m_storage_image};
    m_tracable.descriptors[1] = desc_storage_image;

    // provide inverse modelview and projection matrices
    std::vector<glm::mat4> matrices = {glm::inverse(m_camera->view_matrix()),
                                       glm::inverse(m_camera->projection_matrix())};

    vierkant::descriptor_t desc_matrices = {};
    desc_matrices.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    desc_matrices.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_matrices.buffer = vierkant::Buffer::create(m_device, matrices,
                                                    VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU);
    m_tracable.descriptors[2] = desc_matrices;

    m_tracable.descriptor_set_layout = vierkant::create_descriptor_set_layout(m_device, m_tracable.descriptors);
    VkDescriptorSetLayout set_layout_handle = m_tracable.descriptor_set_layout.get();

    m_tracable.pipeline_info.descriptor_set_layouts = {set_layout_handle};
}

void SimpleRayTracing::update(double time_delta)
{
    m_drawable.matrices.modelview = m_camera->view_matrix();
    m_drawable.matrices.projection = m_camera->projection_matrix();

    // trnasition storage image
    m_storage_image->transition_layout(VK_IMAGE_LAYOUT_GENERAL);

    // tada
    m_ray_tracer.trace_rays(m_tracable);

    // trnasition storage image
    m_storage_image->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // issue top-level draw-command
    m_window->draw();
}

std::vector<VkCommandBuffer> SimpleRayTracing::draw(const vierkant::WindowPtr &w)
{
    auto image_index = w->swapchain().image_index();
    const auto &framebuffer = w->swapchain().framebuffers()[image_index];

    auto render_mesh = [this, &framebuffer]() -> VkCommandBuffer
    {
        if(m_show_ray_tracer){ m_draw_context.draw_image_fullscreen(m_renderer, m_storage_image); }
        else{ m_renderer.stage_drawable(m_drawable); }
        return m_renderer.render(framebuffer);
    };

    auto render_gui = [this, &framebuffer]() -> VkCommandBuffer
    {
        m_gui_context.draw_gui(m_gui_renderer);
        return m_gui_renderer.render(framebuffer);
    };

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
