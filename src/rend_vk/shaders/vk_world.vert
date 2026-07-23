#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec2 in_lm_uv;
layout(location = 3) in vec4 in_color;
layout(location = 4) in uint in_flags;
layout(location = 5) in vec3 in_normal;
layout(location = 6) in float in_time;
layout(location = 7) in float in_refraction_scale;
layout(location = 8) in uint in_effects_enabled;
layout(location = 9) in uint in_refraction_enabled;
layout(location = 10) in vec4 in_sky_axis0;
layout(location = 11) in vec4 in_sky_axis1;
layout(location = 12) in vec4 in_sky_axis2;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_lm_uv;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_flags;
layout(location = 4) out vec3 out_world_pos;
layout(location = 5) out vec3 out_normal;
layout(location = 6) flat out float out_time;
layout(location = 7) flat out float out_refraction_scale;
layout(location = 8) flat out uint out_effects_enabled;
layout(location = 9) flat out uint out_refraction_enabled;
layout(location = 10) flat out vec3 out_sky_axis0;
layout(location = 11) flat out vec3 out_sky_axis1;
layout(location = 12) flat out vec3 out_sky_axis2;

void main() {
    const uint VK_WORLD_VERTEX_SKY = 128u;
    vec3 world_pos = in_pos;
    vec4 clip;
    if ((in_flags & VK_WORLD_VERTEX_SKY) != 0u) {
        // Sky vertices are static local cube coordinates.  Apply the legacy
        // sky rotation here and cancel view translation so the cube remains
        // camera-relative without rebuilding it on the CPU each frame.
        vec3 sky_pos = vec3(dot(in_sky_axis0.xyz, in_pos),
                            dot(in_sky_axis1.xyz, in_pos),
                            dot(in_sky_axis2.xyz, in_pos));
        vec3 view_direction = (push_data.view * vec4(sky_pos, 0.0)).xyz;
        clip = push_data.proj * vec4(view_direction, 1.0);
        world_pos = sky_pos;
    } else {
        clip = push_data.proj * push_data.view * vec4(in_pos, 1.0);
    }
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;

    vec2 uv = in_uv;
    if (in_effects_enabled != 0u) {
        if ((in_flags & 8u) != 0u) {
            float scroll_speed = (in_flags & 1u) != 0u ? 0.5 : 1.6;
            uv.x -= scroll_speed * in_time;
        }
    }

    out_uv = uv;
    out_lm_uv = in_lm_uv;
    out_color = in_color;
    out_flags = in_flags;
    out_world_pos = world_pos;
    out_normal = in_normal;
    out_time = in_time;
    out_refraction_scale = in_refraction_scale;
    out_effects_enabled = in_effects_enabled;
    out_refraction_enabled = in_refraction_enabled;
    out_sky_axis0 = in_sky_axis0.xyz;
    out_sky_axis1 = in_sky_axis1.xyz;
    out_sky_axis2 = in_sky_axis2.xyz;
}
