//
// Created by crocdialer on 11/15/20.
//

#pragma once

#include <vierkant/Device.hpp>
#include <vierkant/Mesh.hpp>
#include <vierkant/PipelineCache.hpp>
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

    struct tracable_t
    {
        //! information for a raytracing pipeline
        raytracing_pipeline_info_t pipeline_info = {};

        //! dimensions for ray-generation
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

    void add_mesh(const vierkant::MeshPtr& mesh, const glm::mat4 &transform = glm::mat4(1));

    const VkPhysicalDeviceRayTracingPipelinePropertiesKHR &properties() const{ return m_properties; };

    /**
     * @brief   trace_rays invokes a raytracing pipeline.
     *
     * @param   tracable
     */
    void trace_rays(tracable_t tracable);

    vierkant::AccelerationStructurePtr acceleration_structure() const{ return m_top_level.structure; }

private:

    //! used for both bottom and toplevel acceleration-structures
    struct acceleration_asset_t
    {
        vierkant::AccelerationStructurePtr structure = nullptr;
        VkDeviceAddress device_address = 0;
        vierkant::BufferPtr buffer = nullptr;
        glm::mat4 transform = glm::mat4(1);
    };

    struct shader_binding_table_t
    {
        vierkant::BufferPtr buffer;

        //! helper enum to create a shader-binding-table
        enum Group : uint32_t
        {
            Raygen = 0,
            Hit = 1,
            Miss = 2,
            Callable = 3,
            MAX_ENUM
        };

        union
        {
            struct
            {
                VkStridedDeviceAddressRegionKHR raygen = {};
                VkStridedDeviceAddressRegionKHR hit = {};
                VkStridedDeviceAddressRegionKHR miss = {};
                VkStridedDeviceAddressRegionKHR callable = {};
            };
            VkStridedDeviceAddressRegionKHR strided_address_region[Group::MAX_ENUM];
        };
    };

    shader_binding_table_t create_shader_binding_table(VkPipeline pipeline,
                                                       const vierkant::raytracing_shader_map_t &shader_stages);

    void set_function_pointers();

    acceleration_asset_t create_acceleration_asset(VkAccelerationStructureCreateInfoKHR create_info,
                                                   const glm::mat4 &transform = glm::mat4(1));

    void create_toplevel_structure();

    vierkant::DevicePtr m_device;

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_properties = {};

    acceleration_asset_t m_top_level = {};

    std::unordered_map<vierkant::MeshPtr, std::vector<acceleration_asset_t>> m_acceleration_assets;

    vierkant::CommandPoolPtr m_command_pool;

    vierkant::DescriptorPoolPtr m_descriptor_pool;

    vierkant::PipelineCachePtr m_pipeline_cache;

    std::unordered_map<VkPipeline, shader_binding_table_t> m_binding_tables;

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
    PFN_vkCmdWriteAccelerationStructuresPropertiesKHR vkCmdWriteAccelerationStructuresPropertiesKHR = nullptr;
    PFN_vkCmdCopyAccelerationStructureKHR vkCmdCopyAccelerationStructureKHR = nullptr;
};

}// namespace vierkant

