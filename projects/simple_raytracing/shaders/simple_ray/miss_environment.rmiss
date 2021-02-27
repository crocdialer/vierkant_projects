#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "ray_common.glsl"

layout(binding = 11) uniform samplerCube u_sampler_cube;

layout(location = 0) rayPayloadInEXT hit_record_t hit_record;

void main()
{
    hit_record.color = texture(u_sampler_cube, gl_WorldRayDirectionEXT).rgb;
}