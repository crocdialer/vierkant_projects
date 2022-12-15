//
// Created by crocdialer on 11/20/20.
//

#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/optional.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include "glm_cereal.hpp"

#include <crocore/set_lru.hpp>
#include <crocore/NamedId.hpp>

#include <vierkant/Window.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/DepthOfField.hpp>
#include <vierkant/CameraControl.hpp>
#include <vierkant/Mesh.hpp>
#include <vierkant/bc7.hpp>
#include <vierkant/model_loading.hpp>

namespace vierkant
{

template<class Archive>
void serialize(Archive &archive, vierkant::AABB &aabb)
{
    archive(cereal::make_nvp("min", aabb.min),
            cereal::make_nvp("max", aabb.max));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Sphere &sphere)
{
    archive(cereal::make_nvp("center", sphere.center),
            cereal::make_nvp("radius", sphere.radius));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Cone &cone)
{
    archive(cereal::make_nvp("axis", cone.axis),
            cereal::make_nvp("cutoff", cone.cutoff));
}

template<class Archive>
void serialize(Archive &archive, vierkant::vertex_attrib_t &vertex_attrib)
{
    archive(cereal::make_nvp("buffer_offset", vertex_attrib.buffer_offset),
//          cereal::make_nvp("buffer", vertex_attrib.buffer),
            cereal::make_nvp("offset", vertex_attrib.offset),
            cereal::make_nvp("stride", vertex_attrib.stride),
            cereal::make_nvp("format", vertex_attrib.format),
            cereal::make_nvp("input_rate", vertex_attrib.input_rate));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Mesh::lod_t &lod)
{
    archive(cereal::make_nvp("base_index", lod.base_index),
            cereal::make_nvp("num_indices", lod.num_indices),
            cereal::make_nvp("base_meshlet", lod.base_meshlet),
            cereal::make_nvp("num_meshlets", lod.num_meshlets));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Mesh::entry_t &entry)
{
    archive(cereal::make_nvp("name", entry.name),
            cereal::make_nvp("transform", entry.transform),
            cereal::make_nvp("bounding_box", entry.bounding_box),
            cereal::make_nvp("bounding_sphere", entry.bounding_sphere),
            cereal::make_nvp("node_index", entry.node_index),
            cereal::make_nvp("vertex_offset", entry.vertex_offset),
            cereal::make_nvp("num_vertices", entry.num_vertices),
            cereal::make_nvp("lods", entry.lods),
            cereal::make_nvp("material_index", entry.material_index),
            cereal::make_nvp("primitive_type", entry.primitive_type),
            cereal::make_nvp("morph_vertex_offset", entry.morph_vertex_offset),
            cereal::make_nvp("morph_weights", entry.morph_weights),
            cereal::make_nvp("enabled", entry.enabled));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Mesh::meshlet_t &meshlet)
{
    archive(cereal::make_nvp("vertex_offset", meshlet.vertex_offset),
            cereal::make_nvp("triangle_offset", meshlet.triangle_offset),
            cereal::make_nvp("vertex_count", meshlet.vertex_count),
            cereal::make_nvp("triangle_count", meshlet.triangle_count),
            cereal::make_nvp("bounding_sphere", meshlet.bounding_sphere),
            cereal::make_nvp("normal_cone", meshlet.normal_cone));
}

template<class Archive>
void serialize(Archive &archive, vierkant::mesh_buffer_bundle_t &mesh_buffer_bundle)
{
    archive(cereal::make_nvp("vertex_stride", mesh_buffer_bundle.vertex_stride),
            cereal::make_nvp("vertex_attribs", mesh_buffer_bundle.vertex_attribs),
            cereal::make_nvp("entries", mesh_buffer_bundle.entries),
            cereal::make_nvp("num_materials", mesh_buffer_bundle.num_materials),
            cereal::make_nvp("vertex_buffer", mesh_buffer_bundle.vertex_buffer),
            cereal::make_nvp("index_buffer", mesh_buffer_bundle.index_buffer),
            cereal::make_nvp("bone_vertex_buffer", mesh_buffer_bundle.bone_vertex_buffer),
            cereal::make_nvp("morph_buffer", mesh_buffer_bundle.morph_buffer),
            cereal::make_nvp("num_morph_targets", mesh_buffer_bundle.num_morph_targets),
            cereal::make_nvp("meshlets", mesh_buffer_bundle.meshlets),
            cereal::make_nvp("meshlet_vertices", mesh_buffer_bundle.meshlet_vertices),
            cereal::make_nvp("meshlet_triangles", mesh_buffer_bundle.meshlet_triangles));
}

template<class Archive>
void serialize(Archive &archive, vierkant::Window::create_info_t &createInfo)
{
    archive(cereal::make_nvp("size", createInfo.size),
            cereal::make_nvp("position", createInfo.position),
            cereal::make_nvp("fullscreen", createInfo.fullscreen),
            cereal::make_nvp("vsync", createInfo.vsync),
            cereal::make_nvp("monitor_index", createInfo.monitor_index),
            cereal::make_nvp("sample_count", createInfo.sample_count),
            cereal::make_nvp("title", createInfo.title)
    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::PBRDeferred::settings_t &render_settings)
{
    archive(cereal::make_nvp("resolution", render_settings.resolution),
            cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("debug_draw_ids", render_settings.debug_draw_ids),
            cereal::make_nvp("frustum_culling", render_settings.frustum_culling),
            cereal::make_nvp("occlusion_culling", render_settings.occlusion_culling),
            cereal::make_nvp("enable_lod", render_settings.enable_lod),
            cereal::make_nvp("indirect_draw", render_settings.indirect_draw),
            cereal::make_nvp("use_meshlet_pipeline", render_settings.use_meshlet_pipeline),
            cereal::make_nvp("tesselation", render_settings.tesselation),
            cereal::make_nvp("wireframe", render_settings.wireframe),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("use_taa", render_settings.use_taa),
            cereal::make_nvp("use_fxaa", render_settings.use_fxaa),
            cereal::make_nvp("environment_factor", render_settings.environment_factor),
            cereal::make_nvp("tonemap", render_settings.tonemap),
            cereal::make_nvp("bloom", render_settings.bloom),
            cereal::make_nvp("gamma", render_settings.gamma),
            cereal::make_nvp("exposure", render_settings.exposure),
            cereal::make_nvp("dof", render_settings.dof)
    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::PBRPathTracer::settings_t &render_settings)
{
    archive(cereal::make_nvp("resolution", render_settings.resolution),
            cereal::make_nvp("max num batches", render_settings.max_num_batches),
            cereal::make_nvp("num_samples", render_settings.num_samples),
            cereal::make_nvp("max_trace_depth", render_settings.max_trace_depth),
            cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("use_denoiser", render_settings.denoising),
            cereal::make_nvp("tonemap", render_settings.tonemap),
            cereal::make_nvp("bloom", render_settings.bloom),
            cereal::make_nvp("environment_factor", render_settings.environment_factor),
            cereal::make_nvp("gamma", render_settings.gamma),
            cereal::make_nvp("exposure", render_settings.exposure),
            cereal::make_nvp("depth_of_field", render_settings.depth_of_field),
            cereal::make_nvp("aperture", render_settings.aperture),
            cereal::make_nvp("focal_distance", render_settings.focal_distance)
    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::dof_settings_t &dof_settings)
{
    archive(cereal::make_nvp("enabled", dof_settings.enabled),
            cereal::make_nvp("focal_depth", dof_settings.focal_depth),
            cereal::make_nvp("focal_length", dof_settings.focal_length),
            cereal::make_nvp("fstop", dof_settings.fstop),
            cereal::make_nvp("circle_of_confusion_sz", dof_settings.circle_of_confusion_sz),
            cereal::make_nvp("gain", dof_settings.gain),
            cereal::make_nvp("fringe", dof_settings.fringe),
            cereal::make_nvp("max_blur", dof_settings.max_blur),
            cereal::make_nvp("auto_focus", dof_settings.auto_focus),
            cereal::make_nvp("debug_focus", dof_settings.debug_focus)

    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::CameraControl &camera_control)
{
    archive(cereal::make_nvp("enabled", camera_control.enabled));
    archive(cereal::make_nvp("mouse_sensitivity", camera_control.mouse_sensitivity));
}

template<class Archive>
void serialize(Archive &archive, vierkant::FlyCamera &fly_camera)
{
    archive(cereal::base_class<vierkant::CameraControl>(&fly_camera),
            cereal::make_nvp("position", fly_camera.position),
            cereal::make_nvp("spherical_coords", fly_camera.spherical_coords),
            cereal::make_nvp("move_speed", fly_camera.move_speed));
}

template<class Archive>
void serialize(Archive &archive, vierkant::OrbitCamera &orbit_camera)
{
    archive(cereal::base_class<vierkant::CameraControl>(&orbit_camera),
            cereal::make_nvp("spherical_coords", orbit_camera.spherical_coords),
            cereal::make_nvp("distance", orbit_camera.distance),
            cereal::make_nvp("look_at", orbit_camera.look_at));
}

}// namespace vierkant

namespace cereal
{

template<class Archive>
void serialize(Archive &archive, vierkant::bc7::block_t &block)
{
    archive(cereal::make_nvp("value", block.value));
}

template<class Archive>
void serialize(Archive &archive,
               vierkant::bc7::compress_result_t &compress_result)
{
    archive(cereal::make_nvp("base_width", compress_result.base_width),
            cereal::make_nvp("base_height", compress_result.base_height),
            cereal::make_nvp("levels", compress_result.levels));
}

template<class Archive>
void serialize(Archive &archive,
               vierkant::model::asset_bundle_t &asset_bundle)
{
    archive(cereal::make_nvp("mesh_buffer_bundle", asset_bundle.mesh_buffer_bundle),
            cereal::make_nvp("compressed_images", asset_bundle.compressed_images));
}

template<class Archive, class T>
void serialize(Archive &archive, crocore::set_lru<T> &set_lru)
{
    std::vector<T> array(set_lru.begin(), set_lru.end());
    archive(array);
    set_lru = {array.begin(), array.end()};
}

template<class Archive, class T>
std::string save_minimal(Archive const &, const crocore::NamedId<T> &named_id)
{
    return named_id.str();
}

template<class Archive, class T>
void load_minimal(Archive const &,
                  crocore::NamedId<T> &named_id,
                  const std::string &uuid_str)
{
    named_id = crocore::NamedId<T>(uuid_str);
}

}// namespace cereal


