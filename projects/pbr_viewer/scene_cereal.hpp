#pragma once

#include "pbr_viewer.hpp"
#include "scene_data.hpp"
#include "serialization.hpp"

template<class Archive>
void serialize(Archive &ar, PBRViewer::settings_t &settings)
{
    ar(cereal::make_nvp("use_validation", settings.use_validation),
       cereal::make_nvp("use_debug_labels", settings.use_debug_labels),
       cereal::make_nvp("log_level", settings.log_level), cereal::make_nvp("log_file", settings.log_file),
       cereal::make_nvp("recent_files", settings.recent_files), cereal::make_nvp("window", settings.window_info),
       cereal::make_nvp("pbr_settings", settings.pbr_settings),
       cereal::make_nvp("path_tracer_settings", settings.path_tracer_settings),
       cereal::make_nvp("draw_ui", settings.draw_ui),
       cereal::make_nvp("ui_draw_view_controls", settings.ui_draw_view_controls),
       cereal::make_nvp("font_url", settings.font_url), cereal::make_nvp("ui_scale", settings.ui_scale),
       cereal::make_nvp("ui_font_scale", settings.ui_font_scale), cereal::make_nvp("draw_grid", settings.draw_grid),
       cereal::make_nvp("draw_aabbs", settings.draw_aabbs), cereal::make_nvp("draw_physics", settings.draw_physics),
       cereal::make_nvp("draw_node_hierarchy", settings.draw_node_hierarchy),
       cereal::make_nvp("path_tracing", settings.path_tracing),
       cereal::make_nvp("texture_compression", settings.texture_compression),
       cereal::make_nvp("mesh_buffer_params", settings.mesh_buffer_params),
       cereal::make_nvp("cache_mesh_bundles", settings.cache_mesh_bundles),
       cereal::make_nvp("cache_zip_archive", settings.cache_zip_archive),
       cereal::make_nvp("enable_raytracing_pipeline_features", settings.enable_raytracing_pipeline_features),
       cereal::make_nvp("enable_ray_query_features", settings.enable_ray_query_features),
       cereal::make_nvp("enable_mesh_shader_device_features", settings.enable_mesh_shader_device_features),
       cereal::make_nvp("orbit_camera", settings.orbit_camera), cereal::make_nvp("fly_camera", settings.fly_camera),
       cereal::make_nvp("use_fly_camera", settings.use_fly_camera),
       cereal::make_nvp("ortho_camera", settings.ortho_camera),
       cereal::make_nvp("current_guizmo", settings.current_guizmo),
       cereal::make_nvp("object_overlay_mode", settings.object_overlay_mode),
       cereal::make_nvp("target_fps", settings.target_fps),
       cereal::make_optional_nvp("playback_speed", settings.playback_speed, 1.f),
       cereal::make_optional_nvp("animation_playback", settings.animation_playback, true));
}

template<class Archive>
void serialize(Archive &ar, mesh_state_t &mesh_state)
{
    ar(cereal::make_nvp("mesh_id", mesh_state.mesh_id), cereal::make_nvp("mesh_library", mesh_state.mesh_library),
       cereal::make_nvp("entry_indices", mesh_state.entry_indices));
}

template<class Archive>
void serialize(Archive &ar, scene_node_t &scene_node)
{
    ar(cereal::make_nvp("name", scene_node.name), cereal::make_optional_nvp("enabled", scene_node.enabled, true),
       cereal::make_nvp("transform", scene_node.transform), cereal::make_optional_nvp("children", scene_node.children),
       cereal::make_optional_nvp("scene_id", scene_node.scene_id),
       cereal::make_optional_nvp("mesh_state", scene_node.mesh_state),
       cereal::make_optional_nvp("animation_state", scene_node.animation_state),
       cereal::make_optional_nvp("physics_state", scene_node.physics_state),
       cereal::make_optional_nvp("constraints", scene_node.constraints));
}

template<class Archive>
void serialize(Archive &ar, scene_camera_t &camera)
{
    ar(cereal::make_nvp("name", camera.name), cereal::make_nvp("transform", camera.transform),
       cereal::make_nvp("params", camera.params));
}

template<class Archive>
void serialize(Archive &ar, scene_data_t &scene_data)
{
    ar(cereal::make_optional_nvp("name", scene_data.name),
       cereal::make_optional_nvp("environment_path", scene_data.environment_path),
       cereal::make_optional_nvp("scene_paths", scene_data.scene_paths),
       cereal::make_nvp("model_paths", scene_data.model_paths), cereal::make_nvp("nodes", scene_data.nodes),
       cereal::make_nvp("scene_roots", scene_data.scene_roots), cereal::make_nvp("cameras", scene_data.cameras),
       cereal::make_optional_nvp("material_bundle_path", scene_data.material_bundle_path));
}

template<class Archive>
void serialize(Archive &ar, material_data_t &material_data)
{
    ar(cereal::make_nvp("materials", material_data.materials), cereal::make_nvp("textures", material_data.textures),
       cereal::make_nvp("texture_samplers", material_data.texture_samplers));
}