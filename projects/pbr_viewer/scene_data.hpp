#pragma once

#include <vierkant/Mesh.hpp>
#include <vierkant/Scene.hpp>
#include <vierkant/animation.hpp>
#include <vierkant/physics_context.hpp>

struct mesh_state_t
{
    vierkant::MeshId mesh_id = vierkant::MeshId::nil();
    std::optional<std::unordered_set<uint32_t>> entry_indices = {};
    bool mesh_library = false;
};

struct scene_node_t
{
    //! a descriptive name
    std::string name;

    //! indicating if node is enabled
    bool enabled = true;

    //! rigid transformation
    vierkant::transform_t transform = {};

    //! list of child-nodes (indices into scene_data_t::nodes)
    std::vector<uint32_t> children = {};

    //! optional sub-scene-id.
    std::optional<vierkant::SceneId> scene_id;

    //! optional mesh-state
    std::optional<mesh_state_t> mesh_state;

    //! optional animation-state
    std::optional<vierkant::animation_component_t> animation_state = {};

    //! optional physics-state
    std::optional<vierkant::physics_component_t> physics_state = {};
};

struct scene_camera_t
{
    std::string name;
    vierkant::transform_t transform = {};
    vierkant::camera_params_variant_t params = {};
};

struct scene_data_t
{
    //! descriptive name for the scene
    std::string name;

    //! map of sub-scenes (.json)
    std::unordered_map<vierkant::SceneId, std::string> scene_paths;

    //! array of file-paths, containing model-files (.gltf, .glb, .obj)
    std::unordered_map<vierkant::MeshId, std::string> model_paths;

    std::string environment_path;
    std::vector<scene_node_t> nodes;

    //! indices into scene_data_t::nodes
    std::vector<uint32_t> scene_roots;

    std::vector<scene_camera_t> cameras;
    std::unordered_map<vierkant::MaterialId, vierkant::material_t> materials;
};