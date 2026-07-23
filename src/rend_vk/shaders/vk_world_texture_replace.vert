#version 450
layout(push_constant) uniform Push { mat4 proj; mat4 view; } push_data;
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 4) in uint in_flags;
layout(location = 0) out vec2 out_uv;
layout(location = 3) flat out uint out_flags;
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
layout(location = 4) out vec3 out_world_pos;
#endif
void main() {
    vec4 clip = push_data.proj * push_data.view * vec4(in_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_uv = in_uv;
    out_flags = in_flags;
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
    out_world_pos = in_pos;
#endif
}
