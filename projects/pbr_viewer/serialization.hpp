//
// Created by crocdialer on 11/20/20.
//

#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/types/deque.hpp>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>

#include "glm_cereal.hpp"

#include <vierkant/Window.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/DepthOfField.hpp>
#include <vierkant/CameraControl.hpp>

namespace vierkant
{

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
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("use_taa", render_settings.use_taa),
            cereal::make_nvp("use_fxaa", render_settings.use_fxaa),
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
    archive(
            cereal::base_class<vierkant::CameraControl>(&orbit_camera),
            cereal::make_nvp("spherical_coords", orbit_camera.spherical_coords),
            cereal::make_nvp("distance", orbit_camera.distance),
            cereal::make_nvp("look_at", orbit_camera.look_at));
}

}// namespace vierkant


