#version 450

// The native sky-array path has the same fog behaviour as the regular world
// shader, but samples the six compatible sky faces from one 2D array. The
// first ten vec4 values deliberately retain the ShadowPages UBO prefix so the
// set=2 binding remains ABI-compatible with vk_shadow.c.
#define VK_WORLD_VERTEX_SKY 128u
#define VK_FOG_SKY 4u

layout(set = 0, binding = 0) uniform sampler2DArray tex_sampler;
layout(std140, set = 2, binding = 1) uniform ShadowPages {
    vec4 shadow_global;
    vec4 shadow_sun;
    vec4 shadow_moment_tuning;
    vec4 shadow_glowmap_tuning;
    vec4 shadow_dlight_count;
    vec4 view_origin;
    vec4 shadow_fog_color_density;
    vec4 shadow_heightfog_start;
    vec4 shadow_heightfog_end;
    vec4 shadow_fog_params;
};

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_lm_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) flat in uint in_flags;
layout(location = 4) in vec3 in_world_pos;
layout(location = 10) flat in vec3 in_sky_axis0;
layout(location = 11) flat in vec3 in_sky_axis1;
layout(location = 12) flat in vec3 in_sky_axis2;

layout(location = 0) out vec4 out_color;

vec4 sample_portal_sky(vec3 world_pos) {
    // Apply the inverse of the static fallback cube rotation, then reproduce
    // the legacy OpenGL cubemap's face choice and face-local coordinates.
    vec3 direction = world_pos - view_origin.xyz;
    direction = vec3(
        dot(vec3(in_sky_axis0.x, in_sky_axis1.x, in_sky_axis2.x), direction),
        dot(vec3(in_sky_axis0.y, in_sky_axis1.y, in_sky_axis2.y), direction),
        dot(vec3(in_sky_axis0.z, in_sky_axis1.z, in_sky_axis2.z), direction));
    vec3 absolute_direction = abs(direction);
    int face;
    float denominator;
    float s;
    float t;
    if (absolute_direction.x > absolute_direction.y &&
        absolute_direction.x > absolute_direction.z) {
        if (direction.x < 0.0) {
            face = 1;
            denominator = -direction.x;
            s = direction.y / denominator;
        } else {
            face = 0;
            denominator = direction.x;
            s = -direction.y / denominator;
        }
        t = direction.z / denominator;
    } else if (absolute_direction.y > absolute_direction.z &&
               absolute_direction.y > absolute_direction.x) {
        if (direction.y < 0.0) {
            face = 3;
            denominator = -direction.y;
            s = -direction.x / denominator;
        } else {
            face = 2;
            denominator = direction.y;
            s = direction.x / denominator;
        }
        t = direction.z / denominator;
    } else {
        denominator = max(absolute_direction.z, 0.000001);
        if (direction.z < 0.0) {
            face = 5;
            s = -direction.y / denominator;
            t = direction.x / denominator;
        } else {
            face = 4;
            s = -direction.y / denominator;
            t = -direction.x / denominator;
        }
    }
    const float inset = 1.0 / 512.0;
    vec2 uv = vec2(clamp((s + 1.0) * 0.5, inset, 1.0 - inset),
                   1.0 - clamp((t + 1.0) * 0.5, inset, 1.0 - inset));
    return texture(tex_sampler, vec3(uv, float(face)));
}

void main() {
    vec4 color = (in_flags & VK_WORLD_VERTEX_SKY) != 0u
        ? texture(tex_sampler, vec3(in_uv, in_lm_uv.x))
        : sample_portal_sky(in_world_pos);
    color *= in_color;
    uint fog_flags = uint(shadow_fog_params.w + 0.5);
    if ((fog_flags & VK_FOG_SKY) != 0u) {
        color.rgb = mix(color.rgb, shadow_fog_color_density.rgb,
                        shadow_fog_params.z);
    }
    out_color = color;
}
