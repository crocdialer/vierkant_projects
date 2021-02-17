#include <crocore/filesystem.hpp>

#include <vierkant/imgui/imgui_util.h>
#include <vierkant/assimp.hpp>
#include <vierkant/MeshNode.hpp>

#include "vierkant_projects/simple_raytracing_shaders.hpp"
#include "simple_raytrace.hpp"

VkFormat vk_format(const crocore::ImagePtr &img)
{
    VkFormat ret = VK_FORMAT_UNDEFINED;

    switch(img->num_components())
    {
        case 1:
            ret = VK_FORMAT_R8_UNORM;
            break;
        case 2:
            ret = VK_FORMAT_R8G8_UNORM;
            break;
        case 3:
            ret = VK_FORMAT_R8G8B8_UNORM;
            break;
        case 4:
            ret = VK_FORMAT_R8G8B8A8_UNORM;
            break;
    }
    return ret;
}

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
    device_info.extensions = vierkant::RayTracer::required_extensions();

    // pass populated extension-chain to enable the features
    device_info.create_device_pNext = &acceleration_structure_features;

    // create a device
    m_device = vk::Device::create(device_info);

    // create a swapchain
    m_window->create_swapchain(m_device, m_use_msaa ? m_device->max_usable_samples() : VK_SAMPLE_COUNT_1_BIT, V_SYNC);

    // create our raytracing-thingies
    vierkant::RayTracer::create_info_t ray_tracer_create_info = {};
    ray_tracer_create_info.num_frames_in_flight = m_window->swapchain().framebuffers().size();
    m_ray_tracer = vierkant::RayTracer(m_device, ray_tracer_create_info);
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

    // attach drag/drop mouse-delegate
    vierkant::mouse_delegate_t file_drop_delegate = {};
    file_drop_delegate.file_drop = [this](const vierkant::MouseEvent &e, const std::vector<std::string> &files)
    {
        auto &f = files.back();

        switch(crocore::filesystem::get_file_type(f))
        {
            case crocore::filesystem::FileType::MODEL:
                load_model(f);
                break;

            default:
                break;
        }
    };
    m_window->mouse_delegates["filedrop"] = file_drop_delegate;

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

void SimpleRayTracing::load_model(const std::filesystem::path &path)
{
    // additionally required buffer-flags for raytracing
    auto buffer_flags = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    auto load_mesh = [this, buffer_flags](const std::filesystem::path &path) -> vierkant::MeshPtr
    {
        auto mesh_assets = vierkant::assimp::load_model(path, background_queue());

        vierkant::Mesh::create_info_t mesh_create_info = {};
        mesh_create_info.buffer_usage_flags = buffer_flags;
        auto mesh = vk::Mesh::create_with_entries(m_device, mesh_assets.entry_create_infos, mesh_create_info);

        if(!mesh)
        {
            LOG_WARNING << "could not load mesh: " << path;
            return nullptr;
        }

        std::vector<vierkant::BufferPtr> staging_buffers;

        VkQueue queue = m_device->queues(vierkant::Device::Queue::GRAPHICS)[1];

        // command pool for background transfer
        auto command_pool = vierkant::create_command_pool(m_device, vierkant::Device::Queue::GRAPHICS,
                                                          VK_COMMAND_POOL_CREATE_TRANSIENT_BIT);

        auto cmd_buf = vierkant::CommandBuffer(m_device, command_pool.get());

        auto create_texture = [device = m_device, cmd_buf_handle = cmd_buf.handle(), &staging_buffers](
                const crocore::ImagePtr &img) -> vierkant::ImagePtr
        {
            vk::Image::Format fmt;
            fmt.format = vk_format(img);
            fmt.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            fmt.extent = {img->width(), img->height(), 1};
            fmt.use_mipmap = true;
            fmt.address_mode_u = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            fmt.address_mode_v = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            fmt.initial_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            fmt.initial_cmd_buffer = cmd_buf_handle;

            auto vk_img = vk::Image::create(device, nullptr, fmt);
            auto buf = vierkant::Buffer::create(device, img->data(), img->num_bytes(),
                                                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
            vk_img->copy_from(buf, cmd_buf_handle);
            staging_buffers.push_back(std::move(buf));
            return vk_img;
        };

        cmd_buf.begin();

        // skin + bones
        mesh->root_bone = mesh_assets.root_bone;

        // node hierarchy
        mesh->root_node = mesh_assets.root_node;

        // node animations
        mesh->node_animations = std::move(mesh_assets.node_animations);

        mesh->materials.resize(mesh_assets.materials.size());

        for(uint32_t i = 0; i < mesh->materials.size(); ++i)
        {
            auto &material = mesh->materials[i];
            material = vierkant::Material::create();

            material->color = mesh_assets.materials[i].diffuse;
            material->emission = mesh_assets.materials[i].emission;
            material->roughness = mesh_assets.materials[i].roughness;
            material->blending = mesh_assets.materials[i].blending;

            auto color_img = mesh_assets.materials[i].img_diffuse;
            auto emmission_img = mesh_assets.materials[i].img_emission;
            auto normal_img = mesh_assets.materials[i].img_normals;
            auto ao_rough_metal_img = mesh_assets.materials[i].img_ao_roughness_metal;

            if(color_img){ material->textures[vierkant::Material::Color] = create_texture(color_img); }
            if(emmission_img){ material->textures[vierkant::Material::Emission] = create_texture(emmission_img); }
            if(normal_img){ material->textures[vierkant::Material::Normal] = create_texture(normal_img); }

            if(ao_rough_metal_img)
            {
                material->textures[vierkant::Material::Ao_rough_metal] = create_texture(ao_rough_metal_img);
            }
        }

        // submit transfer and sync
        cmd_buf.submit(queue, true);

        return mesh;
    };

    if(path.empty())
    {
        // simple bov geometry
        auto geom = vk::Geometry::Box();
        vierkant::Mesh::create_info_t mesh_create_info = {};

        // additionally required buffer-flags for raytracing
        mesh_create_info.buffer_usage_flags = buffer_flags;
        m_mesh = vk::Mesh::create_from_geometry(m_device, geom, mesh_create_info);
    }
    else{ m_mesh = load_mesh(path); }

    vierkant::AABB aabb;
    for(const auto &entry : m_mesh->entries){ aabb += entry.boundingbox.transform(entry.transform); }
    m_scale = 1.f / glm::length(aabb.half_extents());

    m_drawable = vk::Renderer::create_drawables(m_mesh).front();
    m_drawable.pipeline_format.shader_stages = vierkant::create_shader_stages(m_device,
                                                                              vierkant::ShaderType::UNLIT_COLOR);

    // add the mesh, creating an acceleration-structure for it
    m_ray_builder = vierkant::RayBuilder(m_device);
    m_ray_builder.add_mesh(m_mesh);

    // raygen
    m_tracable.pipeline_info.shader_stages = {{VK_SHADER_STAGE_RAYGEN_BIT_KHR,
                                                      vierkant::create_shader_module(m_device,
                                                                                     vierkant::shaders::simple_ray::raygen_rgen)},
            // miss
                                              {VK_SHADER_STAGE_MISS_BIT_KHR,
                                                      vierkant::create_shader_module(m_device,
                                                                                     vierkant::shaders::simple_ray::miss_rmiss)},

            // closest hit
                                              {VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
                                                      vierkant::create_shader_module(m_device,
                                                                                     vierkant::shaders::simple_ray::closesthit_rchit)}};

    update_trace_descriptors();
}

void SimpleRayTracing::update(double time_delta)
{
    auto model_transform = glm::scale(glm::rotate(glm::mat4(1), static_cast<float >(application_time()), glm::vec3(0, 1, 0)), glm::vec3(m_scale));

    m_drawable.matrices.modelview = m_camera->view_matrix() * model_transform;
    m_drawable.matrices.projection = m_camera->projection_matrix();

    auto &ray_asset = m_ray_assets[m_window->swapchain().image_index()];

    m_ray_builder.add_mesh(m_mesh, model_transform);

    // similar to a fence wait
    ray_asset.semaphore.wait(RENDER_FINISHED);

    ray_asset.semaphore = vierkant::Semaphore(m_device, 0);

    ray_asset.command_buffer.begin();

    // keep-alive workaround
    auto tmp = ray_asset.acceleration_asset;

    // update top-level structure
    ray_asset.acceleration_asset = m_ray_builder.create_toplevel(ray_asset.command_buffer.handle());

    update_trace_descriptors();

    // transition storage image
    m_storage_image->transition_layout(VK_IMAGE_LAYOUT_GENERAL, ray_asset.command_buffer.handle());

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

    vierkant::descriptor_t desc_entries = {};
    desc_entries.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    desc_entries.stage_flags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    desc_entries.buffers = {ray_asset.acceleration_asset.entry_buffer};
    ray_asset.tracable.descriptors[5] = desc_entries;

    vierkant::descriptor_t desc_materials = {};
    desc_materials.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    desc_materials.stage_flags = VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    desc_materials.buffers = {ray_asset.acceleration_asset.material_buffer};
    ray_asset.tracable.descriptors[6] = desc_materials;

    if(!ray_asset.tracable.descriptor_set_layout)
    {
        ray_asset.tracable.descriptor_set_layout = vierkant::create_descriptor_set_layout(m_device,
                                                                                          ray_asset.tracable.descriptors);
    }
    ray_asset.tracable.pipeline_info.descriptor_set_layouts = {ray_asset.tracable.descriptor_set_layout.get()};
}
