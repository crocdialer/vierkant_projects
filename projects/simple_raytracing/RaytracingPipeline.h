//
// Created by crocdialer on 11/15/20.
//

#pragma once

#include <vierkant/Device.hpp>
#include <vierkant/Mesh.hpp>

namespace vierkant
{

//! define a shared handle for a VkAccelerationStructureKHR
using AccelerationStructurePtr = std::shared_ptr<VkAccelerationStructureKHR_T>;

//! define a shared handle for a VkQueryPool
using QueryPoolPtr = std::shared_ptr<VkQueryPool_T>;

QueryPoolPtr create_query_pool(const vierkant::DevicePtr& device, VkQueryType query_type);

class RaytracingPipeline
{
public:

    struct Format
    {

    };

    RaytracingPipeline() = default;

    explicit RaytracingPipeline(const vierkant::DevicePtr &device);

    void add_mesh(vierkant::MeshPtr mesh);

private:

    struct acceleration_asset_t
    {
        AccelerationStructurePtr structure = nullptr;
        vierkant::BufferPtr buffer = nullptr;
    };

    vierkant::DevicePtr m_device;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties = {};

    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::unordered_map<vierkant::MeshPtr, std::vector<acceleration_asset_t>> m_acceleration_assets;

    vierkant::CommandPoolPtr m_command_pool;

    // process-addresses for raytracing related function
//    PFN_vkGetBufferDeviceAddressKHR vkGetBufferDeviceAddressKHR;
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;

    void set_function_pointers();
};

}// namespace vierkant

