#pragma once

#include "optional_nvp_cereal.hpp"
#include <cereal/cereal.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/memory.hpp>
#include <vierkant/physics_context.hpp>

namespace vierkant::collision
{

template<class Archive>
void serialize(Archive &, vierkant::collision::none_t &)
{}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::plane_t &s)
{
    archive(cereal::make_nvp("coefficients", s.coefficients), cereal::make_nvp("half_extents", s.half_extent));
}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::box_t &s)
{
    archive(cereal::make_nvp("half_extents", s.half_extents));
}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::sphere_t &s)
{
    archive(cereal::make_nvp("normal", s.radius));
}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::cylinder_t &s)
{
    archive(cereal::make_nvp("radius", s.radius), cereal::make_nvp("height", s.height));
}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::capsule_t &s)
{
    archive(cereal::make_nvp("radius", s.radius), cereal::make_nvp("height", s.height));
}

template<class Archive>
void serialize(Archive &archive, vierkant::collision::mesh_t &s)
{
    archive(cereal::make_nvp("mesh_id", s.mesh_id), cereal::make_nvp("entry_indices", s.entry_indices),
            cereal::make_nvp("library", s.library), cereal::make_nvp("convex_hull", s.convex_hull),
            cereal::make_nvp("lod_bias", s.lod_bias));
}

}// namespace vierkant::collision

namespace vierkant::constraint
{

template<class Archive>
void serialize(Archive &archive, vierkant::constraint::spring_settings_t &s)
{
    archive(cereal::make_nvp("mode", s.mode), cereal::make_nvp("damping", s.damping),
            cereal::make_nvp("frequency_or_stiffness", s.frequency_or_stiffness));
}

template<class Archive>
void serialize(Archive &, vierkant::constraint::none_t &)
{}

template<class Archive>
void serialize(Archive &archive, vierkant::constraint::point_t &c)
{
    archive(cereal::make_nvp("space", c.space), cereal::make_nvp("point1", c.point1),
            cereal::make_nvp("point2", c.point2));
}

template<class Archive>
void serialize(Archive &archive, vierkant::constraint::distance_t &c)
{
    archive(cereal::make_nvp("space", c.space), cereal::make_nvp("point1", c.point1),
            cereal::make_nvp("point2", c.point2), cereal::make_nvp("min_distance", c.min_distance),
            cereal::make_nvp("max_distance", c.max_distance), cereal::make_nvp("spring_settings", c.spring_settings));
}

}// namespace vierkant::constraint

namespace vierkant
{
template<class Archive>
void serialize(Archive &archive, vierkant::physics_component_t &c)
{
    archive(cereal::make_optional_nvp("body_id", c.body_id), cereal::make_nvp("shape", c.shape),
            cereal::make_nvp("shape_transform", c.shape_transform), cereal::make_nvp("mass", c.mass),
            cereal::make_nvp("friction", c.friction), cereal::make_nvp("restitution", c.restitution),
            cereal::make_nvp("linear_damping", c.linear_damping),
            cereal::make_nvp("angular_damping", c.angular_damping), cereal::make_nvp("kinematic", c.kinematic),
            cereal::make_nvp("sensor", c.sensor));
}

template<class Archive>
void serialize(Archive &archive, vierkant::constraint_component_t::body_constraint_t &c)
{
    archive(cereal::make_nvp("constraint", c.constraint), cereal::make_nvp("body_id1", c.body_id1),
            cereal::make_nvp("body_id2", c.body_id2));
}

template<class Archive>
void serialize(Archive &archive, vierkant::constraint_component_t &c)
{
    archive(cereal::make_nvp("constraints", c.constraints));
}

}// namespace vierkant
