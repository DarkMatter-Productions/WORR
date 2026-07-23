#version 450

layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
} push_data;

// Texture-replace inline BSP faces need only their diffuse UVs and the merged
// flags that preserve the intensity convention. Keep the model-space transform
// identical to vk_entity_gpu_bmodel.vert while avoiding normal, lightmap, and
// color stream work that the specialized fragment shaders cannot consume.
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 5) in uint in_face_flags;
layout(location = 6) in vec3 in_origin;
layout(location = 7) in vec4 in_scaled_axis0;
layout(location = 8) in vec4 in_scaled_axis1;
layout(location = 9) in vec4 in_scaled_axis2;
layout(location = 14) in uint in_entity_flags;

layout(location = 0) out vec2 out_uv;
layout(location = 3) flat out uint out_flags;
#ifndef VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG
layout(location = 4) out vec3 out_world_pos;
#endif

void main() {
    vec3 world_pos = in_origin +
        in_pos.x * in_scaled_axis0.xyz +
        in_pos.y * in_scaled_axis1.xyz +
        in_pos.z * in_scaled_axis2.xyz;
    vec4 clip = push_data.proj * push_data.view * vec4(world_pos, 1.0);
    clip.z = (clip.z + clip.w) * 0.5;
    gl_Position = clip;
    out_uv = in_uv;
    out_flags = in_face_flags | in_entity_flags;
#ifndef VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG
    out_world_pos = world_pos;
#endif
}
