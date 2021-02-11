#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

struct entry_t
{
    uint buffer_index;
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
layout(std430, binding = 3, set = 0, scalar) readonly buffer Vertices { Vertex v[]; } vertices[];

// array of index-buffers
layout(std430, binding = 4, set = 0) readonly buffer Indices { uint i[]; } indices[];

// the ray-payload written here
layout(location = 0) rayPayloadInEXT vec3 hitValue;

// builtin barycentric coords
hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    // Object of this instance
    uint objId = 0;//scnDesc.i[gl_InstanceCustomIndexEXT].objId;

    // Indices of the triangle
    ivec3 ind = ivec3(indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 0],
                      indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 1],
                      indices[nonuniformEXT(objId)].i[3 * gl_PrimitiveID + 2]);

    // Vertex of the triangle
    Vertex v0 = vertices[nonuniformEXT(objId)].v[ind.x];
    Vertex v1 = vertices[nonuniformEXT(objId)].v[ind.y];
    Vertex v2 = vertices[nonuniformEXT(objId)].v[ind.z];

    vec4 color = v0.color * barycentricCoords.x + v1.color * barycentricCoords.y + v2.color * barycentricCoords.z;

    hitValue = color.rgb;//barycentricCoords;
}
