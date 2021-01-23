//
// Created by crocdialer on 11/15/20.
//

#pragma once

#include <vierkant/Device.hpp>
#include <vierkant/Mesh.hpp>
#include <vierkant/descriptor.hpp>

namespace vierkant
{

//! define a shared handle for a VkQueryPool
using QueryPoolPtr = std::shared_ptr<VkQueryPool_T>;

QueryPoolPtr create_query_pool(const vierkant::DevicePtr &device,
                               uint32_t query_count,
                               VkQueryType query_type);

/**
 * @brief   Raytracer can be used to run raytracing pipelines.
 *
 */
class Raytracer
{
public:

//    struct Format
//    {
//        VkBuildAccelerationStructureFlagsKHR flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR |
//                                                     VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR;
//    };

    struct tracable_t
    {
        MeshPtr mesh;

        uint32_t entry_index = 0;

        raytracing_pipeline_info_t pipeline_info = {};

//        matrix_struct_t matrices = {};
//
//        material_struct_t material = {};

        VkExtent3D extent = {};

        //! a descriptormap
        descriptor_map_t descriptors;

        //! optional descriptor-set-layout
        DescriptorSetLayoutPtr descriptor_set_layout;
    };

    //! return an array listing all required device-extensions for a raytracing-pipeline.
    static std::vector<const char *> required_extensions();

    Raytracer() = default;

    explicit Raytracer(const vierkant::DevicePtr &device);

    void add_mesh(vierkant::MeshPtr mesh);

    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &properties() const{ return m_properties; };

    void trace_rays(tracable_t tracable);

private:

    struct acceleration_asset_t
    {
        vierkant::AccelerationStructurePtr structure = nullptr;
        VkDeviceAddress device_address = 0;
        vierkant::BufferPtr buffer = nullptr;
    };

    struct shader_binding_table_t
    {
        vierkant::BufferPtr buffer;

        union
        {
            struct
            {
                VkStridedDeviceAddressRegionKHR raygen = {};

                VkStridedDeviceAddressRegionKHR miss = {};

                VkStridedDeviceAddressRegionKHR hit = {};

                VkStridedDeviceAddressRegionKHR callable = {};
            };
            VkStridedDeviceAddressRegionKHR strided_address_region[4];
        };
    };

    shader_binding_table_t create_shader_binding_table(VkPipeline pipeline,
                                                       const vierkant::raytracing_shader_map_t &shader_stages);

    void set_function_pointers();

    acceleration_asset_t create_acceleration_asset(VkAccelerationStructureCreateInfoKHR create_info);

    void create_toplevel_structure();

    vierkant::DevicePtr m_device;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties = {};

    VkPipeline m_pipeline = VK_NULL_HANDLE;

    acceleration_asset_t m_top_level = {};

    std::unordered_map<vierkant::MeshPtr, std::vector<acceleration_asset_t>> m_acceleration_assets;

    vierkant::CommandPoolPtr m_command_pool;

    vierkant::DescriptorPoolPtr m_descriptor_pool;

    // process-addresses for raytracing related functions
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkBuildAccelerationStructuresKHR vkBuildAccelerationStructuresKHR = nullptr;
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkWriteAccelerationStructuresPropertiesKHR vkWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
};

}// namespace vierkant

