//
// Created by crocdialer on 11/20/20.
//

#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/types/variant.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include "glm_cereal.hpp"
#include "animation_cereal.hpp"
#include "collision_cereal.hpp"

#include <crocore/set_lru.hpp>
#include <crocore/NamedId.hpp>

#include "vierkant/model/model_loading.hpp"
#include <vierkant/CameraControl.hpp>
#include <vierkant/Mesh.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/Window.hpp>
#include <vierkant/bc7.hpp>
#include <vierkant/transform.hpp>

namespace crocore
{

template<class Archive, class T>
std::string save_minimal(Archive const &, const crocore::NamedUUID<T> &named_id)
{
    return named_id.str();
}

template<class Archive, class T>
void load_minimal(Archive const &,
                  crocore::NamedUUID<T> &named_id,
                  const std::string &uuid_str)
{
    named_id = crocore::NamedUUID<T>(uuid_str);
}

template<class Archive, class T>
void serialize(Archive &archive, crocore::set_lru<T> &set_lru)
{
    std::vector<T> array(set_lru.begin(), set_lru.end());
    archive(array);
    set_lru = {array.begin(), array.end()};
}

}

namespace vierkant
{

template<class Archive>
void serialize(Archive &archive, vierkant::Geometry &g)
{
    archive(cereal::make_nvp("topology", g.topology),
            cereal::make_nvp("positions", g.positions),
            cereal::make_nvp("colors", g.colors),
            cereal::make_nvp("tex_coords", g.tex_coords),
            cereal::make_nvp("normals", g.normals),
            cereal::make_nvp("tangents", g.tangents),
            cereal::make_nvp("bone_indices", g.bone_indices),
            cereal::make_nvp("bone_weights", g.bone_weights),
            cereal::make_nvp("indices", g.indices));
}

template<class Archive, class T>
void serialize(Archive &archive, vierkant::transform_t_<T> &t)
{
    archive(cereal::make_nvp("translation", t.translation),
            cereal::make_nvp("rotation", t.rotation),
            cereal::make_nvp("scale", t.scale));
}

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
void serialize(Archive &archive, vierkant::material_t &material)
{
    archive(cereal::make_nvp("id", material.id),
            cereal::make_nvp("name", material.name),
            cereal::make_nvp("base_color", material.base_color),
            cereal::make_nvp("emission", material.emission),
            cereal::make_nvp("emissive_strength", material.emissive_strength),
            cereal::make_nvp("roughness", material.roughness),
            cereal::make_nvp("metalness", material.metalness),
            cereal::make_nvp("occlusion", material.occlusion),
            cereal::make_nvp("null_surface", material.null_surface),
            cereal::make_nvp("twosided", material.twosided),
            cereal::make_nvp("ior", material.ior),
            cereal::make_nvp("attenuation_color", material.attenuation_color),
            cereal::make_nvp("transmission", material.transmission),
            cereal::make_nvp("attenuation_distance", material.attenuation_distance),
            cereal::make_nvp("phase_asymmetry_g", material.phase_asymmetry_g),
            cereal::make_nvp("scattering_ratio", material.scattering_ratio),
            cereal::make_nvp("thickness", material.thickness),
            cereal::make_nvp("blend_mode", material.blend_mode),
            cereal::make_nvp("alpha_cutoff", material.alpha_cutoff),
            cereal::make_nvp("specular_factor", material.specular_factor),
            cereal::make_nvp("specular_color", material.specular_color),
            cereal::make_nvp("clearcoat_factor", material.clearcoat_factor),
            cereal::make_nvp("clearcoat_roughness_factor", material.clearcoat_roughness_factor),
            cereal::make_nvp("sheen_color", material.sheen_color),
            cereal::make_nvp("sheen_roughness", material.sheen_roughness),
            cereal::make_nvp("iridescence_factor", material.iridescence_factor),
            cereal::make_nvp("iridescence_ior", material.iridescence_ior),
            cereal::make_nvp("iridescence_thickness_range", material.iridescence_thickness_range),
            cereal::make_nvp("texture_transform", material.texture_transform),
            cereal::make_nvp("textures", material.textures),
            cereal::make_nvp("samplers", material.samplers));
}

template<class Archive>
void serialize(Archive &archive, vierkant::texture_sampler_t &state)
{
    archive(cereal::make_nvp("min_filter", state.min_filter),
            cereal::make_nvp("mag_filter", state.mag_filter),
            cereal::make_nvp("address_mode_u", state.address_mode_u),
            cereal::make_nvp("address_mode_v", state.address_mode_v),
            cereal::make_nvp("transform", state.transform));
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
void serialize(Archive &archive, vierkant::Mesh::entry_create_info_t &entry_info)
{
    archive(cereal::make_nvp("name", entry_info.name),
            cereal::make_nvp("geometry", entry_info.geometry),
            cereal::make_nvp("transform", entry_info.transform),
            cereal::make_nvp("node_index", entry_info.node_index),
            cereal::make_nvp("material_index", entry_info.material_index),
            cereal::make_nvp("morph_targets", entry_info.morph_targets),
            cereal::make_nvp("morph_weights", entry_info.morph_weights));
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
            cereal::make_nvp("morph_weights", entry.morph_weights));
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
void serialize(Archive &archive, vierkant::mesh_buffer_params_t &params)
{
    archive(cereal::make_nvp("remap_indices", params.remap_indices),
            cereal::make_nvp("optimize_vertex_cache", params.optimize_vertex_cache),
            cereal::make_nvp("generate_lods", params.generate_lods),
            cereal::make_nvp("max_num_lods", params.max_num_lods),
            cereal::make_nvp("lod_shrink_factor", params.lod_shrink_factor),
            cereal::make_nvp("generate_meshlets", params.generate_meshlets),
            cereal::make_nvp("use_vertex_colors", params.use_vertex_colors),
            cereal::make_nvp("pack_vertices", params.pack_vertices),
            cereal::make_nvp("meshlet_max_vertices", params.meshlet_max_vertices),
            cereal::make_nvp("meshlet_max_triangles", params.meshlet_max_triangles),
            cereal::make_nvp("meshlet_cone_weight", params.meshlet_cone_weight));
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
            cereal::make_nvp("output_resolution", render_settings.output_resolution),
            cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("debug_draw_ids", render_settings.debug_draw_ids),
            cereal::make_nvp("frustum_culling", render_settings.frustum_culling),
            cereal::make_nvp("occlusion_culling", render_settings.occlusion_culling),
            cereal::make_nvp("enable_lod", render_settings.enable_lod),
            cereal::make_nvp("indirect_draw", render_settings.indirect_draw),
            cereal::make_nvp("use_meshlet_pipeline", render_settings.use_meshlet_pipeline),
            cereal::make_nvp("use_ray_queries", render_settings.use_ray_queries),
            cereal::make_nvp("tesselation", render_settings.tesselation),
            cereal::make_nvp("wireframe", render_settings.wireframe),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("use_taa", render_settings.use_taa),
            cereal::make_nvp("use_fxaa", render_settings.use_fxaa),
            cereal::make_nvp("environment_factor", render_settings.environment_factor),
            cereal::make_nvp("tonemap", render_settings.tonemap),
            cereal::make_nvp("ambient_occlusion", render_settings.ambient_occlusion),
            cereal::make_nvp("max_ao_distance", render_settings.max_ao_distance),
            cereal::make_nvp("bloom", render_settings.bloom),
            cereal::make_nvp("gamma", render_settings.gamma),
            cereal::make_nvp("exposure", render_settings.exposure),
            cereal::make_nvp("depth_of_field", render_settings.depth_of_field)
    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::PBRPathTracer::settings_t &render_settings)
{
    archive(cereal::make_nvp("resolution", render_settings.resolution),
            cereal::make_nvp("max num batches", render_settings.max_num_batches),
            cereal::make_nvp("num_samples", render_settings.num_samples),
            cereal::make_nvp("max_trace_depth", render_settings.max_trace_depth),
            cereal::make_nvp("suspend_trace_when_done", render_settings.suspend_trace_when_done),
            cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("compaction", render_settings.compaction),
            cereal::make_nvp("use_denoiser", render_settings.denoising),
            cereal::make_nvp("tonemap", render_settings.tonemap),
            cereal::make_nvp("bloom", render_settings.bloom),
            cereal::make_nvp("environment_factor", render_settings.environment_factor),
            cereal::make_nvp("gamma", render_settings.gamma),
            cereal::make_nvp("exposure", render_settings.exposure),
            cereal::make_nvp("depth_of_field", render_settings.depth_of_field)
    );
}

template<class Archive>
void serialize(Archive &archive, vierkant::ortho_camera_params_t &cam)
{
    archive(cereal::make_nvp("left", cam.left),
            cereal::make_nvp("right", cam.right),
            cereal::make_nvp("bottom", cam.bottom),
            cereal::make_nvp("top", cam.top),
            cereal::make_nvp("near", cam.near_),
            cereal::make_nvp("far", cam.far_));
}

template<class Archive>
void serialize(Archive &archive, vierkant::physical_camera_params_t &params)
{
    archive(cereal::make_nvp("focal_length", params.focal_length),
            cereal::make_nvp("sensor_width", params.sensor_width),
            cereal::make_nvp("clipping_distances", params.clipping_distances),
            cereal::make_nvp("focal_distance", params.focal_distance),
            cereal::make_nvp("fstop", params.fstop)
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

namespace vierkant::model
{

template<class Archive>
void serialize(Archive &archive,
               vierkant::model::model_assets_t &mesh_assets)
{
    archive(cereal::make_nvp("geometry_data", mesh_assets.geometry_data),
            cereal::make_nvp("materials", mesh_assets.materials),
            cereal::make_nvp("textures", mesh_assets.textures),
            cereal::make_nvp("texture_samplers", mesh_assets.texture_samplers),
//            cereal::make_nvp("lights", mesh_assets.lights),
//            cereal::make_nvp("cameras", mesh_assets.cameras),
            cereal::make_nvp("root_node", mesh_assets.root_node),
            cereal::make_nvp("root_bone", mesh_assets.root_bone),
            cereal::make_nvp("node_animations", mesh_assets.node_animations));
}

}// namespace vierkant::model

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

}// namespace cereal


