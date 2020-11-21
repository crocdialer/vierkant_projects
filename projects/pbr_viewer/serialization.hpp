//
// Created by crocdialer on 11/20/20.
//

#pragma once

#include <cereal/cereal.hpp>
#include <cereal/archives/json.hpp>
#include <cereal/archives/binary.hpp>


#include <vierkant/math.hpp>
#include <vierkant/Window.hpp>
#include <vierkant/SceneRenderer.hpp>

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
void serialize(Archive &archive, vierkant::SceneRenderer::settings_t &render_settings)
{
    archive(cereal::make_nvp("disable_material", render_settings.disable_material),
            cereal::make_nvp("draw_skybox", render_settings.draw_skybox),
            cereal::make_nvp("draw_grid", render_settings.draw_grid),
            cereal::make_nvp("use_fxaa", render_settings.use_fxaa),
            cereal::make_nvp("use_bloom", render_settings.use_bloom),
            cereal::make_nvp("gamma", render_settings.gamma),
            cereal::make_nvp("exposure", render_settings.exposure)
    );
}

}

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


}// namespace vierkant
