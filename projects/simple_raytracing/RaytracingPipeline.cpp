//
// Created by crocdialer on 11/15/20.
//

#include "RaytracingPipeline.h"

namespace vierkant
{

QueryPoolPtr create_query_pool(const vierkant::DevicePtr &device, VkQueryType query_type)
{
    // Allocate a query pool for storing the needed size for every BLAS compaction.
    VkQueryPoolCreateInfo pool_create_info = {};
    pool_create_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;

    pool_create_info.queryCount = 1;
    pool_create_info.queryType = query_type;

    VkQueryPool handle = VK_NULL_HANDLE;
    vkCheck(vkCreateQueryPool(device->handle(), &pool_create_info, nullptr, &handle),
            "could not create VkQueryPool");
    return QueryPoolPtr(handle, [device](VkQueryPool p){ vkDestroyQueryPool(device->handle(), p, nullptr); });
}

inline VkTransformMatrixKHR vk_transform_matrix(const glm::mat4 &m)
{
    VkTransformMatrixKHR ret;

    for(uint32_t i = 0; i < 4; ++i)
    {
        ret.matrix[0][i] = m[i].x;
        ret.matrix[1][i] = m[i].y;
        ret.matrix[2][i] = m[i].z;
    }
    return ret;
}

RaytracingPipeline::RaytracingPipeline(const vierkant::DevicePtr &device) :
        m_device(device)
{
    // get the ray tracing and acceleration structure related function pointers
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
    VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

    std::vector<VkAccelerationStructureGeometryKHR> geometries(mesh->entries.size());
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> offsets(mesh->entries.size());
    std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_infos(mesh->entries.size());

    std::vector<acceleration_asset_t> entry_assets(mesh->entries.size());

    for(uint32_t i = 0; i < mesh->entries.size(); ++i)
    {
        const auto &entry = mesh->entries[i];

        // throw on non-triangle entries
        if(entry.primitive_type != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
        {
            throw std::runtime_error("non-triangle entry");
        }

        VkAccelerationStructureGeometryTrianglesDataKHR triangles = {};
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.indexType = mesh->index_type;
        triangles.indexData.deviceAddress = index_base_address;

        triangles.vertexFormat = vertex_attrib.format;
        triangles.vertexData.deviceAddress = vertex_base_address;
        triangles.vertexStride = vertex_attrib.stride;
        triangles.maxVertex = entry.num_vertices;

//        // convert entry transform
//        transforms[i] = vk_transform_matrix(entry.transform);

//        // TODO: looks wonky -> try shoving transforms into BottomLvl-transforms
//        triangles.transformData.hostAddress = &transforms[i];
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

//    // why!?
//    std::vector<const VkAccelerationStructureBuildGeometryInfoKHR*> build_info_ptrs(build_infos.size());
//    for(uint32_t i = 0; i < build_infos.size(); ++i){ build_info_ptrs[i] = &build_infos[i]; }


        // query memory requirements
        VkAccelerationStructureBuildSizesInfoKHR size_info = {};
        size_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;

        vkGetAccelerationStructureBuildSizesKHR(m_device->handle(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
                                                &build_infos[i], &offsets[i].primitiveCount, &size_info);


        auto &acceleration_asset = entry_assets[i];
        acceleration_asset.buffer = vierkant::Buffer::create(m_device, nullptr, size_info.accelerationStructureSize,
                                                             VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                             VMA_MEMORY_USAGE_GPU_ONLY);

        VkAccelerationStructureCreateInfoKHR create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
        create_info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        create_info.size = size_info.accelerationStructureSize;
        create_info.buffer = acceleration_asset.buffer->handle();

        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;

        // create acceleration structure
        vkCheck(vkCreateAccelerationStructureKHR(m_device->handle(), &create_info, nullptr, &handle),
                "could not create acceleration structure");

        acceleration_asset.structure = AccelerationStructurePtr(handle, [&](VkAccelerationStructureKHR s)
        {
            vkDestroyAccelerationStructureKHR(m_device->handle(), s, nullptr);
        });

        // Allocate the scratch buffers holding the temporary data of the
        // acceleration structure builder
        auto scratch_buffer = vierkant::Buffer::create(m_device, nullptr, size_info.buildScratchSize,
                                                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY);

        // Is enable_compaction requested?
        bool enable_compaction = (flags & VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR)
                                 == VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;

        auto query_pool = create_query_pool(m_device, VK_QUERY_TYPE_ACCELERATION_STRUCTURE_COMPACTED_SIZE_KHR);

    }

    // store bottom-level entries
    if(!entry_assets.empty()){ m_acceleration_assets[mesh] = std::move(entry_assets); }
}

void RaytracingPipeline::set_function_pointers()
{
//    vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(vkGetDeviceProcAddr(
//            m_device->handle(), "vkGetBufferDeviceAddressKHR"));
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
}

}//namespace vierkant
