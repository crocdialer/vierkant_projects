//
// Created by crocdialer on 11/15/20.
//

#pragma once

#include <vierkant/Device.hpp>
#include <vierkant/Mesh.hpp>

namespace vierkant
{

using AccelerationStructurePtr = std::shared_ptr<VkAccelerationStructureKHR_T>;

class RaytracingPipeline
{
public:

    struct Format
    {

    };

    RaytracingPipeline() = default;

    RaytracingPipeline(const vierkant::DevicePtr &device);

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
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR;

    void set_function_pointers();
};

}// namespace vierkant

