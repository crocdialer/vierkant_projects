//! simple struct to groupt rayhit-parameters
struct hit_record_t
{
    // ray/object intersection
    bool intersection;

    // worldspace position
    vec3 position;

    // worldspace normal
    vec3 normal;

    // material color
    vec3 color;
};

struct push_constants_t
{
    float time;
};