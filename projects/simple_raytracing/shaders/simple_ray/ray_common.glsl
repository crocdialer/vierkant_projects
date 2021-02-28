#define PI 3.1415926535897932384626433832795
#define ONE_OVER_PI 0.31830988618379067153776752674503

struct Ray
{
    vec3 origin;
    vec3 direction;
};

//! simple struct to groupt rayhit-parameters
struct payload_t
{
    // the ray that generated this payload
    Ray ray;

    // terminate path
    bool stop;

    // worldspace position
    vec3 position;

    // worldspace normal
    vec3 normal;

    // faceforward normal
    vec3 ffnormal;

    // accumulated radiance along a path
    vec3 radiance;

    // material absorbtion
    vec3 beta;
};

struct push_constants_t
{
    //! current time since start in seconds
    float time;

    //! sample-batch index
    uint batch_index;
};

//! random number generation using pcg32i_random_t, using inc = 1. Our random state is a uint.
uint step_RNG(uint rngState)
{
    return rngState * 747796405 + 1;
}

//! steps the RNG and returns a floating-point value between 0 and 1 inclusive.
float rng_float(inout uint rngState)
{
    // condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    rngState  = step_RNG(rngState);
    uint word = ((rngState >> ((rngState >> 28) + 4)) ^ rngState) * 277803737;
    word      = (word >> 22) ^ word;
    return float(word) / 4294967295.0f;
}

//! random point on a unit-sphere, centered at the normal
vec3 rng_lambert(vec2 Xi, vec3 normal)
{
    // [0, 2pi]
    const float theta = 2.0 * PI * Xi.y;

    // [-1, 1]
    float u = 2.0 * Xi.x - 1.0;

    const float r = sqrt(1.0 - u * u);
    vec3 dir = normal + vec3(r * cos(theta), r * sin(theta), u);

    return normalize(dir);
}

vec2 Hammersley(uint i, uint N)
{
    float vdc = float(bitfieldReverse(i)) * 2.3283064365386963e-10; // Van der Corput
    return vec2(float(i) / float(N), vdc);
}

mat3 local_frame(vec3 N)
{
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(N, up));
    vec3 bitangent = cross(N, tangent);
    return mat3(tangent, bitangent, N);
}

vec3 ImportanceSampleCosine(vec2 Xi, vec3 N)
{
    float cosTheta = sqrt(max(1.0 - Xi.y, 0.0));
    float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    float phi = 2.0 * PI * Xi.x;

    vec3 L = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);


    // TODO: replace with local_frame
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(N, up));
    vec3 bitangent = cross(N, tangent);
    return tangent * L.x + bitangent * L.y + N * L.z;
}

// Sample a half-vector in world space
vec3 ImportanceSampleGGX(vec2 Xi, float roughness, vec3 N)
{
    float a = roughness * roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt(clamp((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y), 0.0, 1.0));
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));

    vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

    // TODO: replace with local_frame
    vec3 up = abs(N.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    return tangent * H.x + bitangent * H.y + N * H.z;
}

//vec3 UE4Eval(in Material material, in vec3 bsdfDir)
//{
//    vec3 N = payload.ffnormal;
//    vec3 V = -gl_WorldRayDirectionNV;
//    vec3 L = bsdfDir;
//    vec3 albedo = material.albedo.xyz;
//
//    float NDotL = dot(N, L);
//    float NDotV = dot(N, V);
//
//    if (NDotL <= 0.0 || NDotV <= 0.0)
//    return vec3(0.0);
//
//    vec3 H = normalize(L + V);
//    float NDotH = dot(N, H);
//    float LDotH = dot(L, H);
//
//    // Specular
//    float specular = 0.5;
//    vec3 specularCol = mix(vec3(1.0) * 0.08 * specular, albedo, material.metallic);
//    float a = max(0.001, material.roughness);
//    float D = GTR2(NDotH, a);
//    float FH = SchlickFresnel(LDotH);
//    vec3 F = mix(specularCol, vec3(1.0), FH);
//    float roughg = (material.roughness * 0.5 + 0.5);
//    roughg = roughg * roughg;
//
//    float G = SmithGGX(NDotL, roughg) * SmithGGX(NDotV, roughg);
//
//    // Diffuse + Specular components
//    return (INV_PI * albedo) * (1.0 - material.metallic) + F * D * G;
//}