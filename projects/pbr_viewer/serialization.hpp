//
// Created by crocdialer on 11/20/20.
//

#pragma once

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>


#include <vierkant/math.hpp>
#include <vierkant/Window.hpp>
#include <vierkant/PBRDeferred.hpp>
#include <vierkant/PBRPathTracer.hpp>
#include <vierkant/DepthOfField.hpp>

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
    archive(cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("draw_grid", render_settings.draw_grid),
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
            cereal::make_nvp("exposure", render_settings.exposure)
    );
}

namespace postfx
{
template<class Archive>
void serialize(Archive &archive, vierkant::postfx::dof_settings_t &dof_settings)
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
}// namespace postfx

}// namespace vierkant

namespace glm
{

template<class Archive>
void serialize(Archive &archive, glm::vec2 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y));
}

template<class Archive>
void serialize(Archive &archive, glm::vec3 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z));
}

template<class Archive>
void serialize(Archive &archive, glm::vec4 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w));
}

template<class Archive>
void serialize(Archive &archive, glm::ivec2 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y));
}

template<class Archive>
void serialize(Archive &archive, glm::ivec3 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z));
}

template<class Archive>
void serialize(Archive &archive, glm::ivec4 &v){ archive(v.x, v.y, v.z, v.w); }

template<class Archive>
void serialize(Archive &archive, glm::uvec2 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y));
}

template<class Archive>
void serialize(Archive &archive, glm::uvec3 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z));
}

template<class Archive>
void serialize(Archive &archive, glm::uvec4 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w));
}

template<class Archive>
void serialize(Archive &archive, glm::dvec2 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y));
}

template<class Archive>
void serialize(Archive &archive, glm::dvec3 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z));
}

template<class Archive>
void serialize(Archive &archive, glm::dvec4 &v)
{
    archive(cereal::make_nvp("x", v.x),
            cereal::make_nvp("y", v.y),
            cereal::make_nvp("z", v.z),
            cereal::make_nvp("w", v.w));
}

// glm matrices serialization
template<class Archive>
void serialize(Archive &archive, glm::mat2 &m){ archive(m[0], m[1]); }

template<class Archive>
void serialize(Archive &archive, glm::dmat2 &m){ archive(m[0], m[1]); }

template<class Archive>
void serialize(Archive &archive, glm::mat3 &m){ archive(m[0], m[1], m[2]); }

template<class Archive>
void serialize(Archive &archive, glm::mat4 &m){ archive(m[0], m[1], m[2], m[3]); }

template<class Archive>
void serialize(Archive &archive, glm::dmat4 &m){ archive(m[0], m[1], m[2], m[3]); }

template<class Archive>
void serialize(Archive &archive, glm::quat &q)
{
    archive(cereal::make_nvp("x", q.x),
            cereal::make_nvp("y", q.y),
            cereal::make_nvp("z", q.z),
            cereal::make_nvp("w", q.w));
}

template<class Archive>
void serialize(Archive &archive, glm::dquat &q)
{
    archive(cereal::make_nvp("x", q.x),
            cereal::make_nvp("y", q.y),
            cereal::make_nvp("z", q.z),
            cereal::make_nvp("w", q.w));
}


}// namespace glm
