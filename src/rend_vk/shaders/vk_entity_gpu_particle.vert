#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

// A single instance supplies the particle origin, final world-space scale,
// and modulated palette colour. gl_VertexIndex reconstructs the same three
// expanded vertices and texture coordinates used by the legacy CPU path.
layout(location = 0) in vec4 in_origin_scale;
layout(location = 1) in vec4 in_color;

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec2 out_lm_uv;
layout(location = 2) out vec4 out_color;
layout(location = 3) flat out uint out_flags;
layout(location = 4) out vec3 out_world_pos;
layout(location = 5) out vec3 out_normal;

const float VK_ENTITY_PARTICLE_SIZE = 1.70710678;
const float VK_ENTITY_PARTICLE_SCALE =
    1.0 / (2.0 * VK_ENTITY_PARTICLE_SIZE);
const uint VK_ENTITY_VERTEX_FULLBRIGHT = 1u;
const uint VK_ENTITY_VERTEX_NO_SHADOW = 8u;
const uint VK_ENTITY_VERTEX_NO_DLIGHT = 16u;

void main() {
    // renderer/view_setup.c writes the world-to-view basis with right
    // negated. Recover the two axes used by the CPU VectorMA expansion.
    vec3 right = -vec3(push_data.view[0].x, push_data.view[1].x,
                       push_data.view[2].x);
    vec3 up = vec3(push_data.view[0].y, push_data.view[1].y,
                  push_data.view[2].y);

    vec2 corner;
    vec2 uv;
    if (gl_VertexIndex == 0) {
        corner = vec2(VK_ENTITY_PARTICLE_SCALE,
                      -VK_ENTITY_PARTICLE_SCALE);
        uv = vec2(0.0);
    } else if (gl_VertexIndex == 1) {
        corner = vec2(VK_ENTITY_PARTICLE_SCALE,
                      1.0 - VK_ENTITY_PARTICLE_SCALE);
        uv = vec2(0.0, VK_ENTITY_PARTICLE_SIZE);
    } else {
        corner = vec2(-1.0 + VK_ENTITY_PARTICLE_SCALE,
                      -VK_ENTITY_PARTICLE_SCALE);
        uv = vec2(VK_ENTITY_PARTICLE_SIZE, 0.0);
    }

    vec3 world_pos = in_origin_scale.xyz + in_origin_scale.w *
        (corner.x * right + corner.y * up);
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
