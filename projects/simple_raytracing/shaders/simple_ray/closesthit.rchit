#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_EXT_scalar_block_layout : enable

struct Vertex
{
    vec3 position;
    vec4 color;
    vec3 normal;
    vec3 tangent;
};

// array of vertex-buffers
layout(binding = 3, set = 0, scalar) buffer Vertices { Vertex v[]; } vertices[];

// array of index-buffers
layout(binding = 4, set = 0) buffer Indices { uint i[]; } indices[];

// the ray-payload written here
layout(location = 0) rayPayloadInEXT vec3 hitValue;

// builtin barycentric coords
hitAttributeEXT vec2 attribs;

void main()
{
    const vec3 barycentricCoords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);
    hitValue = barycentricCoords;
}
