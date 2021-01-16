//
// Created by crocdialer on 11/15/20.
//

#include "RaytracingPipeline.h"

namespace vierkant
{

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

inline VkTransformMatrixKHR vk_transform_matrix(const glm::mat4 &m)
{
    VkTransformMatrixKHR ret;
    auto transpose = glm::transpose(m);
    memcpy(&ret, glm::value_ptr(transpose), sizeof(VkTransformMatrixKHR));
    return ret;
}

RaytracingPipeline::RaytracingPipeline(const vierkant::DevicePtr &device) :
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
}

void RaytracingPipeline::add_mesh(vierkant::MeshPtr mesh)
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

        // Since the scratch buffer is reused across builds, we need a barrier to ensure one build
        // is finished before starting the next one
        VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(cmd_buffer.handle(),
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                             0, 1, &barrier, 0, nullptr, 0, nullptr);

        // Write compacted size to query number idx.
        if(enable_compaction)
        {
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

        uint32_t statTotalOriSize{0}, statTotalCompactSize{0};

        for(uint32_t i = 0; i < entry_assets.size(); i++)
        {
            LOG_DEBUG << crocore::format("reducing bottom-lvl-size (%d), from %d to %d \n", i,
                                         (uint32_t) entry_assets[i].buffer->num_bytes(),
                                         compact_sizes[i]);

            statTotalOriSize += (uint32_t) entry_assets[i].buffer->num_bytes();
            statTotalCompactSize += (uint32_t) compact_sizes[i];

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
}

void RaytracingPipeline::set_function_pointers()
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

RaytracingPipeline::acceleration_asset_t
RaytracingPipeline::create_acceleration_asset(VkAccelerationStructureCreateInfoKHR create_info)
{
    RaytracingPipeline::acceleration_asset_t acceleration_asset = {};
    acceleration_asset.buffer = vierkant::Buffer::create(m_device, nullptr, create_info.size,
                                                         VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                         VMA_MEMORY_USAGE_GPU_ONLY);

    create_info.buffer = acceleration_asset.buffer->handle();

    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;

    // create acceleration structure
    vkCheck(vkCreateAccelerationStructureKHR(m_device->handle(), &create_info, nullptr, &handle),
            "could not create acceleration structure");

    acceleration_asset.structure = AccelerationStructurePtr(handle, [&](VkAccelerationStructureKHR s)
    {
        vkDestroyAccelerationStructureKHR(m_device->handle(), s, nullptr);
    });
    return acceleration_asset;
}

}//namespace vierkant
