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
    uint texture_index;
    uint normalmap_index;
    uint emission_index;
    uint ao_rough_metal_index;
};

struct Vertex
{
    vec3 position;
    vec4 color;
    vec2 tex_coord;
    vec3 normal;
    vec3 tangent;
};

layout(push_constant) uniform PushConstants
{
    push_constants_t push_constants;
};

// array of vertex-buffers
layout(binding = 3, set = 0, scalar) readonly buffer Vertices { Vertex v[]; } vertices[];

// array of index-buffers
layout(binding = 4, set = 0) readonly buffer Indices { uint i[]; } indices[];

layout(binding = 5, set = 0) uniform Entries
{
    entry_t u_entries[MAX_NUM_ENTRIES];
};

layout(binding = 6, set = 0) uniform Materials
{
    material_t materials[MAX_NUM_ENTRIES];
};

layout(binding = 7) uniform sampler2D u_albedos[];

layout(binding = 8) uniform sampler2D u_normalmaps[];

layout(binding = 9) uniform sampler2D u_emissionmaps[];

layout(binding = 10) uniform sampler2D u_ao_rough_metal_maps[];

// the ray-payload written here
layout(location = 0) rayPayloadInEXT payload_t payload;

// builtin barycentric coords
hitAttributeEXT vec2 attribs;

// TODO: simplify code with mix
Vertex interpolate_vertex()
{
    const vec3 triangle_coords = vec3(1.0f - attribs.x - attribs.y, attribs.x, attribs.y);

    // entry aka instance
    entry_t entry = u_entries[gl_InstanceCustomIndexEXT];

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

    material_t material = materials[u_entries[gl_InstanceCustomIndexEXT].material_index];

    payload.position = v.position;
    payload.normal = v.normal;

    bool tangent_valid = any(greaterThan(abs(v.tangent), vec3(0.0)));

    if (tangent_valid)
    {
        // sample normalmap
        vec3 normal = normalize(2.0 * (texture(u_normalmaps[material.normalmap_index], v.tex_coord).xyz - vec3(0.5)));

        // normal, tangent, bi-tangent
        vec3 t = normalize(v.tangent);
        vec3 n = normalize(v.normal);
        vec3 b = normalize(cross(n, t));
        mat3 transpose_tbn = mat3(t, b, n);
        payload.normal = transpose_tbn * normal;
    }

    // flip the normal so it points against the ray direction:
    payload.ffnormal = faceforward(payload.normal, gl_WorldRayDirectionEXT, payload.normal);

    // max emission from material/map
    const float emission_tex_gain = 10.0;
    vec3 emission = max(material.emission.rgb, emission_tex_gain * texture(u_emissionmaps[material.emission_index], v.tex_coord).rgb);

    // add radiance from emission
    payload.radiance += payload.beta * emission;

    // modulate beta with albedo
    vec3 color = material.color.rgb * texture(u_albedos[material.texture_index], v.tex_coord).rgb;
    payload.beta *= color;

    // roughness / metalness
    vec3 ao_rough_metal = texture(u_ao_rough_metal_maps[material.ao_rough_metal_index], v.tex_coord).xyz;
    float roughness = material.roughness * ao_rough_metal.y;
    float metalness = material.metalness * ao_rough_metal.z;

    // generate a bounce ray

    // offset position along the normal
    payload.ray.origin = payload.position + 0.0001 * payload.ffnormal;

    // scatter ray direction
    uint rngState = uint(push_constants.batch_index + push_constants.time * (gl_LaunchSizeEXT.x * gl_LaunchIDEXT.y + gl_LaunchIDEXT.x));
    vec2 Xi = vec2(rng_float(rngState), rng_float(rngState));

    // no diffuse rays for metal
    float diffuse_ratio = 0.5 * (1.0 - metalness);
    float reflect_prob = rng_float(rngState);

    if(reflect_prob < diffuse_ratio){ payload.ray.direction = ImportanceSampleCosine(Xi, payload.ffnormal); }
    else
    {
        // possible half-vector from GGX distribution
        vec3 H = ImportanceSampleGGX(Xi, roughness, payload.ffnormal);
        payload.ray.direction = reflect(gl_WorldRayDirectionEXT, H);
    }

    float NoL = max(dot(payload.ffnormal, payload.ray.direction), 0.0);
    if (NoL <= 0.0){ payload.stop = true; }
}
