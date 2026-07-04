#pragma once

#include <vierkant_cereal/scene_data.hpp>
#include <vierkant_cereal/serialization.hpp>

template<class Archive>
void serialize(Archive &ar, mesh_state_t &mesh_state)
{
    ar(cereal::make_nvp("mesh_id", mesh_state.mesh_id),
       cereal::make_optional_nvp("mesh_library", mesh_state.mesh_library),
       cereal::make_optional_nvp("entry_indices", mesh_state.entry_indices),
       cereal::make_optional_nvp("material_ids", mesh_state.material_ids));
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
       cereal::make_optional_nvp("constraints", scene_node.constraints),
       cereal::make_optional_nvp("camera_state", scene_node.camera_state),
       cereal::make_optional_nvp("light_state", scene_node.light_state));
}

template<class Archive>
void serialize(Archive &ar, scene_data_t &scene_data)
{
    ar(cereal::make_optional_nvp("name", scene_data.name),
       cereal::make_optional_nvp("environment_path", scene_data.environment_path),
       cereal::make_optional_nvp("scene_paths", scene_data.scene_paths),
       cereal::make_nvp("model_paths", scene_data.model_paths), cereal::make_nvp("nodes", scene_data.nodes),
       cereal::make_nvp("scene_roots", scene_data.scene_roots),
       cereal::make_optional_nvp("material_bundle_path", scene_data.material_bundle_path),
       cereal::make_optional_nvp("lights", scene_data.lights));
}