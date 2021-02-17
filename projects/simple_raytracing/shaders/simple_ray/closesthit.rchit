#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

const uint MAX_NUM_ENTRIES = 1024;

struct entry_t
{
    // per mesh
    uint buffer_index;
    uint material_index;

    // per entry
    uint base_vertex;
    uint base_index;
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

// the ray-payload written here
layout(location = 0) rayPayloadInEXT vec3 hitValue;

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

    return out_vert;
}

void main()
{
//    vec3 worldPos = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
    Vertex v = interpolate_vertex();
    hitValue = v.color.rgb;
}
