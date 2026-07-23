#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

// Simple and lightning beam segments preserve their CPU-selected facing
// vector, then expand their original two-triangle winding from gl_VertexIndex.
layout(location = 0) in vec3 in_start;
layout(location = 1) in vec3 in_end;
layout(location = 2) in vec3 in_right;
layout(location = 3) in vec4 in_color;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_lm_uv;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_flags;
layout(location = 4) out vec3 out_world_pos;
layout(location = 5) out vec3 out_normal;

const uint VK_ENTITY_VERTEX_FULLBRIGHT = 1u;
const uint VK_ENTITY_VERTEX_NO_SHADOW = 8u;
const uint VK_ENTITY_VERTEX_NO_DLIGHT = 16u;

void main() {
    vec3 world_pos;
    vec2 uv;
    // Matches the legacy emitted order: (0, 2, 3), then (0, 1, 2).
    if (gl_VertexIndex == 0 || gl_VertexIndex == 3) {
        world_pos = in_start + in_right;
        uv = vec2(0.0, 0.0);
    } else if (gl_VertexIndex == 1 || gl_VertexIndex == 5) {
        world_pos = in_end - in_right;
        uv = vec2(1.0, 1.0);
    } else if (gl_VertexIndex == 2) {
        world_pos = in_end + in_right;
        uv = vec2(0.0, 1.0);
    } else {
        world_pos = in_start - in_right;
        uv = vec2(1.0, 0.0);
    }
    vec4 clip = push_data.proj * push_data.view * vec4(world_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_uv = uv;
    out_lm_uv = vec2(0.0);
    out_color = in_color;
    out_flags = VK_ENTITY_VERTEX_FULLBRIGHT |
        VK_ENTITY_VERTEX_NO_SHADOW | VK_ENTITY_VERTEX_NO_DLIGHT;
    out_world_pos = world_pos;
    out_normal = vec3(0.0, 0.0, 1.0);
}
