//
// Created by crocdialer on 2/5/22.
//

#pragma once

#include <cereal/cereal.hpp>
#include <vierkant/math.hpp>

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
