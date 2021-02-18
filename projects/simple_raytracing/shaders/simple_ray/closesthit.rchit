#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_GOOGLE_include_directive : enable

#include "ray_common.glsl"

const uint MAX_NUM_ENTRIES = 1024;

struct entry_t
{
    mat4 modelview;
    mat4 normal_matrix;

    // per mesh
    uint buffer_index;
    uint material_index;

    // per entry
    uint base_vertex;
    uint base_index;
};

struct material_t
{
    vec4 color;
    vec4 emission;
    float metalness;
    float roughness;
};

struct Vertex
{
    vec3 position;
    vec4 color;
    vec2 tex_coord;
    vec3 normal;
    vec3 tangent;
};

// array of vertex-buffers
layout(binding = 3, set = 0, scalar) readonly buffer Vertices { Vertex v[]; } vertices[];

// array of index-buffers
layout(binding = 4, set = 0) readonly buffer Indices { uint i[]; } indices[];

layout(binding = 5, set = 0) uniform Entries
{
    entry_t entries[MAX_NUM_ENTRIES];
};

layout(binding = 6, set = 0) uniform Materials
{
    material_t materials[MAX_NUM_ENTRIES];
};

// the ray-payload written here
layout(location = 0) rayPayloadInEXT hit_record_t hit_record;

// builtin barycentric coords
hitAttributeEXT vec2 attribs;

Vertex interpolate_vertex()
{
    const vec3 triangle_coords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    // entry aka instance
    entry_t entry = entries[gl_InstanceCustomIndexEXT];

    // triangle indices
    ivec3 ind = ivec3(indices[nonuniformEXT(entry.buffer_index)].i[entry.base_index + 3 * gl_PrimitiveID + 0],
    indices[nonuniformEXT(entry.buffer_index)].i[entry.base_index + 3 * gl_PrimitiveID + 1],
    indices[nonuniformEXT(entry.buffer_index)].i[entry.base_index + 3 * gl_PrimitiveID + 2]);

    // triangle vertices
    Vertex v0 = vertices[nonuniformEXT(entry.buffer_index)].v[entry.base_vertex + ind.x];
    Vertex v1 = vertices[nonuniformEXT(entry.buffer_index)].v[entry.base_vertex + ind.y];
    Vertex v2 = vertices[nonuniformEXT(entry.buffer_index)].v[entry.base_vertex + ind.z];

    // interpolated vertex
    Vertex out_vert;
    out_vert.position = v0.position * triangle_coords.x + v1.position * triangle_coords.y + v2.position * triangle_coords.z;
    out_vert.color = v0.color * triangle_coords.x + v1.color * triangle_coords.y + v2.color * triangle_coords.z;
    out_vert.tex_coord = v0.tex_coord * triangle_coords.x + v1.tex_coord * triangle_coords.y + v2.tex_coord * triangle_coords.z;
    out_vert.normal = v0.normal * triangle_coords.x + v1.normal * triangle_coords.y + v2.normal * triangle_coords.z;
    out_vert.tangent = v0.tangent * triangle_coords.x + v1.tangent * triangle_coords.y + v2.tangent * triangle_coords.z;

    // bring surfel into worldspace
    out_vert.position = (entry.modelview * vec4(out_vert.position, 1.0)).xyz;
    out_vert.normal = (entry.normal_matrix * vec4(out_vert.normal, 1.0)).xyz;
    out_vert.tangent = (entry.normal_matrix * vec4(out_vert.tangent, 1.0)).xyz;

    return out_vert;
}

void main()
{
//    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    Vertex v = interpolate_vertex();

    material_t material = materials[entries[gl_InstanceCustomIndexEXT].material_index];

    hit_record.intersection = true;
    hit_record.position = v.position;
    hit_record.normal = v.normal;
    hit_record.color = v.color.rgb * material.color.rgb;
}
