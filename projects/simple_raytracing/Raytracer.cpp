//
// Created by crocdialer on 11/15/20.
//

#include "Raytracer.hpp"

namespace vierkant
{

//! helper enum to create a shader-binding-table
enum BindingTableGroup : uint32_t
{
    Raygen = 0,
    Miss = 1,
    Hit = 2,
    Callable = 3
};

inline uint32_t aligned_size(uint32_t size, uint32_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

inline VkTransformMatrixKHR vk_transform_matrix(const glm::mat4 &m)
{
    VkTransformMatrixKHR ret;
    auto transpose = glm::transpose(m);
    memcpy(&ret, glm::value_ptr(transpose), sizeof(VkTransformMatrixKHR));
    return ret;
}

QueryPoolPtr create_query_pool(const vierkant::DevicePtr &device, uint32_t query_count, VkQueryType query_type)
{
    VkQueryPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;

    pool_create_info.queryCount = query_count;
    pool_create_info.queryType = query_type;

    VkQueryPool handle = VK_NULL_HANDLE;
    vkCheck(vkCreateQueryPool(device->handle(), &pool_create_info, nullptr, &handle),
            "could not create VkQueryPool");
    return QueryPoolPtr(handle, [device](VkQueryPool p){ vkDestroyQueryPool(device->handle(), p, nullptr); });
}

std::vector<const char *> Raytracer::required_extensions()
{
    return {VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
            VK_KHR_RAY_QUERY_EXTENSION_NAME,
            VK_KHR_PIPELINE_LIBRARY_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
}

Raytracer::Raytracer(const vierkant::DevicePtr &device) :
        m_device(device)
{
    // get the ray tracing and acceleration-structure related function pointers
    set_function_pointers();

    // query the ray tracing properties
    m_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR;
    VkPhysicalDeviceProperties2 deviceProps2{};
    deviceProps2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    deviceProps2.pNext = &m_properties;
    vkGetPhysicalDeviceProperties2(m_device->physical_device(), &deviceProps2);

    m_command_pool = vierkant::create_command_pool(device, vierkant::Device::Queue::GRAPHICS,
                                                   VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);

    // we also need a DescriptorPool ...
    vierkant::descriptor_count_t descriptor_counts = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 32},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,              32},
                                                      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,             128},
                                                      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,             256}};
    m_descriptor_pool = vierkant::create_descriptor_pool(m_device, descriptor_counts, 512);
}

void Raytracer::add_mesh(vierkant::MeshPtr mesh)
{

    const auto &vertex_attrib = mesh->vertex_attribs[vierkant::Mesh::AttribLocation::ATTRIB_POSITION];
    VkDeviceAddress vertex_base_address = vertex_attrib.buffer->device_address() + vertex_attrib.offset;
    VkDeviceAddress index_base_address = mesh->index_buffer->device_address();

    // raytracing flags
    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
                                                 VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    // compaction requested?
    bool enable_compaction = (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
                             == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

    std::vector<VkAccelerationStructureGeometryKHR> geometries(mesh->entries.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> offsets(mesh->entries.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_infos(mesh->entries.size());

    // all-in, one scratch buffer per entry is memory-intense but fast
    std::vector<vierkant::BufferPtr> scratch_buffers(mesh->entries.size());

    // one per bottom-lvl-build
    std::vector<vierkant::CommandBuffer> command_buffers(mesh->entries.size());

    // used to query compaction sizes after building
    auto query_pool = create_query_pool(m_device, mesh->entries.size(),
                                        VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR);

    // those will be stored
    std::vector<acceleration_asset_t> entry_assets(mesh->entries.size());

    for(uint32_t i = 0; i < mesh->entries.size(); ++i)
    {
        const auto &entry = mesh->entries[i];

        // throw on non-triangle entries
        if(entry.primitive_type != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        {
            throw std::runtime_error("RaytracingPipeline::add_mesh: provided non-triangle entry");
        }

        VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.indexType = mesh->index_type;
        triangles.indexData.deviceAddress = index_base_address;
        triangles.vertexFormat = vertex_attrib.format;
        triangles.vertexData.deviceAddress = vertex_base_address;
        triangles.vertexStride = vertex_attrib.stride;
        triangles.maxVertex = entry.num_vertices;
        triangles.transformData = {};

        auto &geometry = geometries[i];
        geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
        geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        geometry.geometry.triangles = triangles;

        // offsets
        auto &offset = offsets[i];
        offset.firstVertex = entry.base_vertex;
        offset.primitiveOffset = entry.base_index / 3;
        offset.primitiveCount = entry.num_indices / 3;

        auto &build_info = build_infos[i];
        build_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
        build_info.flags = flags;
        build_info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        build_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        build_info.geometryCount = 1;
        build_info.pGeometries = &geometry;
        build_info.srcAccelerationStructure = VK_NULL_HANDLE;

        // query memory requirements
        VkAccelerationStructureBuildSizesInfoKHR size_info = {};
        size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(m_device->handle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &build_infos[i], &offsets[i].primitiveCount, &size_info);

        auto &acceleration_asset = entry_assets[i];
        VkAccelerationStructureCreateInfoKHR create_info = {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        create_info.size = size_info.accelerationStructureSize;
        acceleration_asset = create_acceleration_asset(create_info);

        // Allocate the scratch buffers holding the temporary data of the
        // acceleration structure builder
        scratch_buffers[i] = vierkant::Buffer::create(m_device, nullptr, size_info.buildScratchSize,
                                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        // assign acceleration structure and scratch_buffer
        build_info.dstAccelerationStructure = acceleration_asset.structure.get();
        build_info.scratchData.deviceAddress = scratch_buffers[i]->device_address();

        // create commandbuffer for building the bottomlevel-structure
        auto &cmd_buffer = command_buffers[i];
        cmd_buffer = vierkant::CommandBuffer(m_device, m_command_pool.get());
        cmd_buffer.begin();

        // build the AS
        const VkAccelerationStructureBuildRangeInfoKHR *offset_ptr = &offset;
        vkCmdBuildAccelerationStructuresKHR(cmd_buffer.handle(), 1, &build_info, &offset_ptr);

        // Write compacted size to query number idx.
        if(enable_compaction)
        {
            // Since the scratch buffer is reused across builds, we need a barrier to ensure one build
            // is finished before starting the next one
            VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
            barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            vkCmdPipelineBarrier(cmd_buffer.handle(),
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                                 0, 1, &barrier, 0, nullptr, 0, nullptr);

            VkAccelerationStructureKHR accel_structure = acceleration_asset.structure.get();
            vkCmdWriteAccelerationStructuresPropertiesKHR(cmd_buffer.handle(), 1, &accel_structure,
                                                          VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR,
                                                          query_pool.get(), i);
        }
        cmd_buffer.end();
    }

    std::vector<VkCommandBuffer> cmd_handles(command_buffers.size());
    for(uint32_t i = 0; i < command_buffers.size(); ++i){ cmd_handles[i] = command_buffers[i].handle(); }

    VkQueue queue = m_device->queue();
    vierkant::submit(m_device, queue, cmd_handles, VK_NULL_HANDLE, true);

    // free scratchbuffer here
    scratch_buffers.clear();

    // memory-compaction for bottom-lvl-structures
    if(enable_compaction)
    {
        auto cmd_buffer = vierkant::CommandBuffer(m_device, m_command_pool.get());
        cmd_buffer.begin();

        // Get the size result back
        std::vector<VkDeviceSize> compact_sizes(mesh->entries.size());
        vkGetQueryPoolResults(m_device->handle(), query_pool.get(), 0,
                              (uint32_t) compact_sizes.size(), compact_sizes.size() * sizeof(VkDeviceSize),
                              compact_sizes.data(), sizeof(VkDeviceSize), VK_QUERY_RESULT_WAIT_BIT);


        // compacting
        std::vector<acceleration_asset_t> entry_assets_compact(entry_assets.size());

        for(uint32_t i = 0; i < entry_assets.size(); i++)
        {
            LOG_DEBUG << crocore::format("reducing bottom-lvl-size (%d), from %d to %d \n", i,
                                         (uint32_t) entry_assets[i].buffer->num_bytes(),
                                         compact_sizes[i]);

            // Creating a compact version of the AS
            VkAccelerationStructureCreateInfoKHR create_info{VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR};
            create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
            create_info.size = compact_sizes[i];
            auto &acceleration_asset = entry_assets_compact[i];
            acceleration_asset = create_acceleration_asset(create_info);

            // copy the original BLAS to a compact version
            VkCopyAccelerationStructureInfoKHR copy_info{VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR};
            copy_info.src = entry_assets[i].structure.get();
            copy_info.dst = acceleration_asset.structure.get();
            copy_info.mode = VK_COPY_ACCELERATION_STRUCTURE_MODE_COMPACT_KHR;
            vkCmdCopyAccelerationStructureKHR(cmd_buffer.handle(), &copy_info);
        }
        cmd_buffer.submit(m_device->queue(), true);

        // keep compacted versions
        entry_assets = std::move(entry_assets_compact);
    }

    // store bottom-level entries
    if(!entry_assets.empty()){ m_acceleration_assets[mesh] = std::move(entry_assets); }

    // tmp here for testing
    create_toplevel_structure();
}

void Raytracer::set_function_pointers()
{
    vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkCmdBuildAccelerationStructuresKHR"));
    vkBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkBuildAccelerationStructuresKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkBuildAccelerationStructuresKHR"));
    vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkCreateAccelerationStructureKHR"));
    vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkDestroyAccelerationStructureKHR"));
    vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkGetAccelerationStructureBuildSizesKHR"));
    vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkGetAccelerationStructureDeviceAddressKHR"));
    vkCmdTraceRaysKHR = reinterpret_cast<PFN_vkCmdTraceRaysKHR>(vkGetDeviceProcAddr(m_device->handle(),
                                                                                    "vkCmdTraceRaysKHR"));
    vkGetRayTracingShaderGroupHandlesKHR = reinterpret_cast<PFN_vkGetRayTracingShaderGroupHandlesKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkGetRayTracingShaderGroupHandlesKHR"));
    vkCreateRayTracingPipelinesKHR = reinterpret_cast<PFN_vkCreateRayTracingPipelinesKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkCreateRayTracingPipelinesKHR"));

    vkWriteAccelerationStructuresPropertiesKHR = reinterpret_cast<PFN_vkWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkWriteAccelerationStructuresPropertiesKHR"));
    vkCmdWriteAccelerationStructuresPropertiesKHR = reinterpret_cast<PFN_vkCmdWriteAccelerationStructuresPropertiesKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkCmdWriteAccelerationStructuresPropertiesKHR"));
    vkCmdCopyAccelerationStructureKHR = reinterpret_cast<PFN_vkCmdCopyAccelerationStructureKHR>(vkGetDeviceProcAddr(
            m_device->handle(), "vkCmdCopyAccelerationStructureKHR"));
}

Raytracer::acceleration_asset_t
Raytracer::create_acceleration_asset(VkAccelerationStructureCreateInfoKHR create_info)
{
    Raytracer::acceleration_asset_t acceleration_asset = {};
    acceleration_asset.buffer = vierkant::Buffer::create(m_device, nullptr, create_info.size,
                                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                         VMA_MEMORY_USAGE_GPU_ONLY);

    create_info.buffer = acceleration_asset.buffer->handle();

    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;

    // create acceleration structure
    vkCheck(vkCreateAccelerationStructureKHR(m_device->handle(), &create_info, nullptr, &handle),
            "could not create acceleration structure");

    // get device address
    VkAccelerationStructureDeviceAddressInfoKHR address_info = {};
    address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    address_info.accelerationStructure = handle;
    acceleration_asset.device_address = vkGetAccelerationStructureDeviceAddressKHR(m_device->handle(), &address_info);

    acceleration_asset.structure = AccelerationStructurePtr(handle, [&](VkAccelerationStructureKHR s)
    {
        vkDestroyAccelerationStructureKHR(m_device->handle(), s, nullptr);
    });
    return acceleration_asset;
}

void Raytracer::create_toplevel_structure()
{
    std::vector<VkAccelerationStructureInstanceKHR> instances;

    // instance flags
    VkGeometryInstanceFlagsKHR instance_flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    for(const auto &[mesh, acceleration_assets] : m_acceleration_assets)
    {
        assert(mesh->entries.size() == acceleration_assets.size());

        for(uint i = 0; i < acceleration_assets.size(); ++i)
        {
            const auto &entry = mesh->entries[i];
            const auto &asset = acceleration_assets[i];

            // per bottom-lvl instance
            VkAccelerationStructureInstanceKHR instance{};
            instance.transform = vk_transform_matrix(entry.transform);
            instance.instanceCustomIndex = 0;
            instance.mask = 0xFF;
            instance.instanceShaderBindingTableRecordOffset = 0;
            instance.flags = instance_flags;
            instance.accelerationStructureReference = asset.device_address;

            instances.push_back(instance);
        }
    }

    // put instances into host-visible gpu-buffer
    auto instance_buffer = vierkant::Buffer::create(m_device, instances, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU);

    VkDeviceOrHostAddressConstKHR instance_data_device_address{};
    instance_data_device_address.deviceAddress = instance_buffer->device_address();

    VkAccelerationStructureGeometryKHR acceleration_structure_geometry{};
    acceleration_structure_geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    acceleration_structure_geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    acceleration_structure_geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    acceleration_structure_geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    acceleration_structure_geometry.geometry.instances.arrayOfPointers = VK_FALSE;
    acceleration_structure_geometry.geometry.instances.data = instance_data_device_address;

    uint32_t num_primitives = 1;

    // The pSrcAccelerationStructure, dstAccelerationStructure, and mode members of pBuildInfo are ignored.
    // Any VkDeviceOrHostAddressKHR members of pBuildInfo are ignored by this command,
    // except that the hostAddress member of VkAccelerationStructureGeometryTrianglesDataKHR::transformData
    // will be examined to check if it is NULL.*
    VkAccelerationStructureBuildGeometryInfoKHR acceleration_structure_build_geometry_info{};
    acceleration_structure_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    acceleration_structure_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    acceleration_structure_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    acceleration_structure_build_geometry_info.geometryCount = 1;
    acceleration_structure_build_geometry_info.pGeometries = &acceleration_structure_geometry;

    VkAccelerationStructureBuildSizesInfoKHR acceleration_structure_build_sizes_info{};
    acceleration_structure_build_sizes_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(m_device->handle(),
                                            VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                            &acceleration_structure_build_geometry_info,
                                            &num_primitives,
                                            &acceleration_structure_build_sizes_info);

    // create the top-level structure
    VkAccelerationStructureCreateInfoKHR create_info = {};
    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    create_info.size = acceleration_structure_build_sizes_info.accelerationStructureSize;

    auto top_level_structure = create_acceleration_asset(create_info);

    LOG_DEBUG << top_level_structure.buffer->num_bytes() << " bytes in toplevel";

    // Create a small scratch buffer used during build of the top level acceleration structure
    auto scratch_buffer = vierkant::Buffer::create(m_device, nullptr,
                                                   acceleration_structure_build_sizes_info.buildScratchSize,
                                                   VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

    VkAccelerationStructureBuildGeometryInfoKHR acceleration_build_geometry_info{};
    acceleration_build_geometry_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    acceleration_build_geometry_info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    acceleration_build_geometry_info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    acceleration_build_geometry_info.mode = m_top_level.structure ? VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR
                                                                  : VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    acceleration_build_geometry_info.srcAccelerationStructure = m_top_level.structure.get();
    acceleration_build_geometry_info.dstAccelerationStructure = top_level_structure.structure.get();
    acceleration_build_geometry_info.geometryCount = 1;
    acceleration_build_geometry_info.pGeometries = &acceleration_structure_geometry;
    acceleration_build_geometry_info.scratchData.deviceAddress = scratch_buffer->device_address();

    VkAccelerationStructureBuildRangeInfoKHR acceleration_structure_build_range_info{};
    acceleration_structure_build_range_info.primitiveCount = instances.size();
    acceleration_structure_build_range_info.primitiveOffset = 0;
    acceleration_structure_build_range_info.firstVertex = 0;
    acceleration_structure_build_range_info.transformOffset = 0;

    auto cmd_buffer = vierkant::CommandBuffer(m_device, m_command_pool.get());
    cmd_buffer.begin();

    // build the AS
    const VkAccelerationStructureBuildRangeInfoKHR *offset_ptr = &acceleration_structure_build_range_info;
    vkCmdBuildAccelerationStructuresKHR(cmd_buffer.handle(), 1, &acceleration_build_geometry_info, &offset_ptr);

    cmd_buffer.submit(m_device->queue(), true);

    m_top_level = top_level_structure;
}

void Raytracer::trace_rays(tracable_t tracable)
{
    // TODO: cache
    auto descriptor_set_layout = vierkant::create_descriptor_set_layout(m_device, tracable.descriptors);
    VkDescriptorSetLayout set_layout_handle = descriptor_set_layout.get();

    tracable.pipeline_info.descriptor_set_layouts = {set_layout_handle};

    // create a raytracing pipeline
    auto pipeline = vierkant::Pipeline::create(m_device, tracable.pipeline_info);

    // create the binding table
    auto binding_table = create_shader_binding_table(pipeline->handle(), tracable.pipeline_info.shader_stages);

    // TODO: cache
    // fetch descriptor set
    auto descriptor_set = vierkant::create_descriptor_set(m_device, m_descriptor_pool, descriptor_set_layout);

    // update descriptor-set with actual descriptors
    vierkant::update_descriptor_set(m_device, descriptor_set, tracable.descriptors);

    VkDescriptorSet descriptor_set_handle = descriptor_set.get();

    auto cmd_buffer = vierkant::CommandBuffer(m_device, m_command_pool.get());
    cmd_buffer.begin();

    // bind raytracing pipeline
    pipeline->bind(cmd_buffer.handle());

    // bind descriptor set (accelearation-structure, uniforms, storage-buffers, samplers, storage-image)
    vkCmdBindDescriptorSets(cmd_buffer.handle(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline->layout(),
                            0, 1, &descriptor_set_handle, 0, nullptr);

    // finally record the tracing command
    vkCmdTraceRaysKHR(cmd_buffer.handle(),
                      &binding_table.raygen,
                      &binding_table.miss,
                      &binding_table.hit,
                      &binding_table.callable,
                      tracable.extent.width, tracable.extent.height, tracable.extent.depth);

    cmd_buffer.submit(m_device->queue(), true);
}

Raytracer::shader_binding_table_t
Raytracer::create_shader_binding_table(VkPipeline pipeline,
                                       const vierkant::raytracing_shader_map_t &shader_stages)
{
    // shader groups
    auto group_create_infos = vierkant::raytracing_shader_groups(shader_stages);

    const uint32_t group_count = group_create_infos.size();
    const uint32_t handle_size = m_properties.shaderGroupHandleSize;
    const uint32_t handle_size_aligned = aligned_size(m_properties.shaderGroupHandleSize,
                                                      m_properties.shaderGroupHandleAlignment);
    const uint32_t binding_table_size = group_count * handle_size_aligned;

    // retrieve the shader-handles into host-memory
    std::vector<uint8_t> shader_handle_data(group_count * handle_size);
    vkCheck(vkGetRayTracingShaderGroupHandlesKHR(m_device->handle(), pipeline, 0, group_count,
                                                 shader_handle_data.size(),
                                                 shader_handle_data.data()),
            "Raytracer::trace_rays: could not retrieve shader group handles");

    shader_binding_table_t binding_table = {};
    binding_table.buffer = vierkant::Buffer::create(m_device, nullptr, binding_table_size,
                                                    VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR |
                                                    VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                    VMA_MEMORY_USAGE_CPU_TO_GPU);

    // copy opaque shader-handles with proper stride (handle_size_aligned)
    auto buf_ptr = static_cast<uint8_t *>(binding_table.buffer->map());
    for(uint32_t i = 0; i < group_count; ++i)
    {
        memcpy(buf_ptr + i * handle_size_aligned, shader_handle_data.data() + i * handle_size, handle_size);
    }
    binding_table.buffer->unmap();

    // this feels a bit silly but these groups do not correspond 1:1 to shader-stages.
    std::map<BindingTableGroup, size_t> group_elements;
    for(const auto &pair : shader_stages)
    {
        switch(pair.first)
        {
            case VK_SHADER_STAGE_RAYGEN_BIT_KHR:
                group_elements[BindingTableGroup::Raygen]++;
                break;

            case VK_SHADER_STAGE_MISS_BIT_KHR:
                group_elements[BindingTableGroup::Miss]++;
                break;

            case VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
            case VK_SHADER_STAGE_ANY_HIT_BIT_KHR:
                group_elements[BindingTableGroup::Hit]++;
                break;

            case VK_SHADER_STAGE_CALLABLE_BIT_KHR:
                group_elements[BindingTableGroup::Callable]++;
                break;

            default:
                break;
        }
    }

    size_t buffer_offset = 0;

    for(const auto &[group, num_elements] : group_elements)
    {
        auto &address_region = binding_table.strided_address_region[group];
        address_region.deviceAddress = binding_table.buffer->device_address() + buffer_offset;
        address_region.stride = handle_size_aligned;
        address_region.size = handle_size_aligned * num_elements;

        buffer_offset += address_region.size;
    }
    return binding_table;
}

}//namespace vierkant
