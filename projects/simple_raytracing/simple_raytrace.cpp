#include <vierkant/imgui/imgui_util.h>

#include "vierkant_projects/simple_raytracing_shaders.hpp"

#include "simple_raytrace.hpp"

void SimpleRayTracing::setup()
{
    crocore::g_logger.set_severity(crocore::Severity::DEBUG);

    create_context_and_window();
    create_graphics_pipeline();
    load_model();
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

    // attach logger for debug-output
    m_instance.set_debug_fn([](const char *msg){ LOG_WARNING << msg; });

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

    VkPhysicalDeviceScalarBlockLayoutFeatures scalar_block_features = {};
    scalar_block_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;

    VkPhysicalDeviceDescriptorIndexingFeatures descriptor_indexing_features = {};
    descriptor_indexing_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

    // create a pNext-chain connecting the extension-structures
    acceleration_structure_features.pNext = &ray_tracing_pipeline_features;
    ray_tracing_pipeline_features.pNext = &ray_query_features;
    ray_query_features.pNext = &scalar_block_features;
    scalar_block_features.pNext = &descriptor_indexing_features;

    // query support for the required device-features
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

    // create a device
    m_device = vk::Device::create(device_info);

    // create a swapchain
    m_window->create_swapchain(m_device, m_use_msaa ? m_device->max_usable_samples() : VK_SAMPLE_COUNT_1_BIT, V_SYNC);

    // create our raytracing-thingies
    vierkant::Raytracer::create_info_t ray_tracer_create_info = {};
    ray_tracer_create_info.num_frames_in_flight = m_window->swapchain().framebuffers().size();
    m_ray_tracer = vierkant::Raytracer(m_device, ray_tracer_create_info);
    m_ray_builder = vierkant::RayBuilder(m_device);

    m_ray_assets.resize(m_window->swapchain().framebuffers().size());

    for(auto &ray_asset : m_ray_assets)
    {
        ray_asset.command_buffer = vierkant::CommandBuffer(m_device, m_device->command_pool());
    }

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
    vk::gui::Context::create_info_t gui_create_info = {};
    gui_create_info.ui_scale = 2.f;
    m_gui_context = vk::gui::Context(m_device, gui_create_info);
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
    m_camera->set_position(glm::vec3(0.f, 1.f, 2.f));

    m_camera->set_look_at(glm::vec3(0.f));
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

    // create a storage image
    vierkant::Image::Format img_format = {};
    img_format.extent = fb_extent;
    img_format.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    img_format.initial_layout = VK_IMAGE_LAYOUT_GENERAL;
    m_storage_image = vierkant::Image::create(m_device, img_format);

    // set extent for trace-pipeline
//    m_tracable.extent = m_storage_image->extent();

    if(m_mesh){ update_trace_descriptors(); }
}

void SimpleRayTracing::load_model()
{
//    // simple triangle geometry
//    auto geom = vk::Geometry::create();
//    geom->vertices = {glm::vec3(-0.5f, -0.5f, 0.f),
//                      glm::vec3(0.5f, -0.5f, 0.f),
//                      glm::vec3(0.f, 0.5f, 0.f)};
//    geom->colors = {glm::vec4(1.f), glm::vec4(1.f), glm::vec4(1.f)};
//    geom->indices = {0, 1, 2};

    auto geom = vk::Geometry::Box();
//    geom->tex_coords.clear();
//    geom->normals.clear();
//    geom->tangents.clear();

    vierkant::Mesh::create_info_t mesh_create_info = {};

    // additionally required buffer-flags for raytracing
    mesh_create_info.buffer_usage_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                          VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    m_mesh = vk::Mesh::create_from_geometry(m_device, geom, mesh_create_info);

    m_drawable = vk::Renderer::create_drawables(m_mesh).front();
    m_drawable.pipeline_format.shader_stages = vierkant::create_shader_stages(m_device,
                                                                              vierkant::ShaderType::UNLIT_COLOR);

    // add the mesh, creating an acceleration-structure for it
    m_ray_builder.add_mesh(m_mesh);

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

    update_trace_descriptors();
}

void SimpleRayTracing::update(double time_delta)
{
    auto model_transform = glm::rotate(glm::mat4(1), static_cast<float >(application_time()), glm::vec3(0, 1, 0));

    m_drawable.matrices.modelview = m_camera->view_matrix() * model_transform;
    m_drawable.matrices.projection = m_camera->projection_matrix();

    auto &ray_asset = m_ray_assets[m_window->swapchain().image_index()];

    m_ray_builder.add_mesh(m_mesh, model_transform);

    // similar to a fence wait
    ray_asset.semaphore.wait(RENDER_FINISHED);

    ray_asset.semaphore = vierkant::Semaphore(m_device, 0);

    ray_asset.command_buffer.begin();

    // update top-level structure
    auto tmp = ray_asset.acceleration_asset;
    ray_asset.acceleration_asset = m_ray_builder.create_toplevel(ray_asset.command_buffer.handle(),
                                                                 ray_asset.acceleration_asset.structure);

    // transition storage image
    m_storage_image->transition_layout(VK_IMAGE_LAYOUT_GENERAL, ray_asset.command_buffer.handle());

    update_trace_descriptors();

    // tada
    m_ray_tracer.trace_rays(ray_asset.tracable, ray_asset.command_buffer.handle());

    // transition storage image
    m_storage_image->transition_layout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, ray_asset.command_buffer.handle());

    ray_asset.command_buffer.end();

    constexpr uint64_t ray_signal_value = RAYTRACING_FINISHED;
    VkTimelineSemaphoreSubmitInfo timeline_info;
    timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timeline_info.pNext = nullptr;
    timeline_info.waitSemaphoreValueCount = 0;
    timeline_info.pWaitSemaphoreValues = nullptr;
    timeline_info.signalSemaphoreValueCount = 1;
    timeline_info.pSignalSemaphoreValues = &ray_signal_value;

    auto semaphore_handle = ray_asset.semaphore.handle();
    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.pNext = &timeline_info;
    submit_info.signalSemaphoreCount = 1;
    submit_info.pSignalSemaphores = &semaphore_handle;
    ray_asset.command_buffer.submit(m_device->queues(vierkant::Device::Queue::GRAPHICS)[1], false, VK_NULL_HANDLE,
                                    submit_info);

    vierkant::semaphore_submit_info_t semaphore_submit_info = {};
    semaphore_submit_info.semaphore = ray_asset.semaphore.handle();
    semaphore_submit_info.wait_value = RAYTRACING_FINISHED;
    semaphore_submit_info.signal_value = RENDER_FINISHED;

    // issue top-level draw-command
    m_window->draw({semaphore_submit_info});

    ray_asset.semaphore.wait(RAYTRACING_FINISHED);
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

void SimpleRayTracing::update_trace_descriptors()
{
    auto &ray_asset = m_ray_assets[m_window->swapchain().image_index()];

    ray_asset.tracable.pipeline_info = m_tracable.pipeline_info;
    ray_asset.tracable.extent = m_storage_image->extent();

    // descriptors
    vierkant::descriptor_t desc_acceleration_structure = {};
    desc_acceleration_structure.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    desc_acceleration_structure.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_acceleration_structure.acceleration_structure = ray_asset.acceleration_asset.structure;
    ray_asset.tracable.descriptors[0] = desc_acceleration_structure;

    vierkant::descriptor_t desc_storage_image = {};
    desc_storage_image.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    desc_storage_image.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_storage_image.image_samplers = {m_storage_image};
    ray_asset.tracable.descriptors[1] = desc_storage_image;

    // provide inverse modelview and projection matrices
    std::vector<glm::mat4> matrices = {glm::inverse(m_camera->view_matrix()),
                                       glm::inverse(m_camera->projection_matrix())};

    vierkant::descriptor_t desc_matrices = {};
    desc_matrices.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    desc_matrices.stage_flags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    desc_matrices.buffers = {vierkant::Buffer::create(m_device, matrices,
                                                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                                      VMA_MEMORY_USAGE_CPU_TO_GPU)};
    ray_asset.tracable.descriptors[2] = desc_matrices;

    vierkant::descriptor_t desc_vertex_buffers = {};
    desc_vertex_buffers.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    desc_vertex_buffers.stage_flags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    desc_vertex_buffers.buffers = {m_mesh->vertex_attribs[vierkant::Mesh::AttribLocation::ATTRIB_POSITION].buffer};
    desc_vertex_buffers.buffer_offsets = {
            m_mesh->vertex_attribs[vierkant::Mesh::AttribLocation::ATTRIB_POSITION].buffer_offset};
    ray_asset.tracable.descriptors[3] = desc_vertex_buffers;

    vierkant::descriptor_t desc_index_buffers = {};
    desc_index_buffers.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    desc_index_buffers.stage_flags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    desc_index_buffers.buffers = {m_mesh->index_buffer};
    desc_index_buffers.buffer_offsets = {m_mesh->index_buffer_offset};
    ray_asset.tracable.descriptors[4] = desc_index_buffers;

    if(!ray_asset.tracable.descriptor_set_layout)
    {
        ray_asset.tracable.descriptor_set_layout = vierkant::create_descriptor_set_layout(m_device,
                                                                                          ray_asset.tracable.descriptors);
    }
    ray_asset.tracable.pipeline_info.descriptor_set_layouts = {ray_asset.tracable.descriptor_set_layout.get()};
}
