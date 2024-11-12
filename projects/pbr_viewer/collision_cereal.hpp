#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/list.hpp>
#include <cereal/types/memory.hpp>
#include <vierkant/physics_context.hpp>

namespace vierkant::collision
{

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
    archive(cereal::make_nvp("mesh_id", s.mesh_id), cereal::make_nvp("convex_hull", s.convex_hull));
}

}// namespace vierkant::collision

namespace vierkant
{
template<class Archive>
void serialize(Archive &archive, vierkant::physics_component_t &c)
{
    archive(cereal::make_nvp("shape", c.shape), cereal::make_nvp("mass", c.mass),
            cereal::make_nvp("friction", c.friction), cereal::make_nvp("restitution", c.restitution),
            cereal::make_nvp("linear_damping", c.linear_damping),
            cereal::make_nvp("angular_damping", c.angular_damping), cereal::make_nvp("kinematic", c.kinematic),
            cereal::make_nvp("sensor", c.sensor));
}
}// namespace vierkant
