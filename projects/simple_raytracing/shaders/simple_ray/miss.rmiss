#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "ray_common.glsl"

layout(location = 0) rayPayloadInEXT hit_record_t hit_record;

// Returns the color of the sky in a given direction (in linear color space)
vec3 sky_color(vec3 direction)
{
    if(direction.y > 0.0f)
    {
        return mix(vec3(1.0f), vec3(0.25f, 0.5f, 1.0f), direction.y);
    }
    return vec3(0.03f);
}

void main()
{
    hit_record.intersection = false;
    hit_record.color = sky_color(gl_WorldRayDirectionEXT);//vec3(0.0, 0.0, 0.2);
}