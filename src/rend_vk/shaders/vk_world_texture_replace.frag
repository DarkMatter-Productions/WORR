#version 450
#define VK_WORLD_VERTEX_INTENSITY 32u
#define VK_FOG_GLOBAL 1u
#define VK_FOG_HEIGHT 2u
layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(std140, set = 2, binding = 1) uniform ShadowPages {
    vec4 shadow_global; vec4 shadow_sun; vec4 shadow_moment_tuning;
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
    vec4 shadow_glowmap_tuning; vec4 shadow_dlight_count; vec4 view_origin;
    vec4 shadow_fog_color_density; vec4 shadow_heightfog_start;
    vec4 shadow_heightfog_end; vec4 shadow_fog_params;
#endif
};
layout(location = 0) in vec2 in_uv;
layout(location = 3) flat in uint in_flags;
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
layout(location = 4) in vec3 in_world_pos;
#endif
layout(location = 0) out vec4 out_color;
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
void apply_fog(inout vec3 diffuse) {
    uint flags = uint(shadow_fog_params.w + 0.5);
    float depth = gl_FragCoord.z / max(gl_FragCoord.w, 1e-6);
    if ((flags & VK_FOG_GLOBAL) != 0u) {
        float d = shadow_fog_color_density.a * depth;
        diffuse = mix(diffuse, shadow_fog_color_density.rgb, 1.0 - exp(-(d * d)));
    }
    if ((flags & VK_FOG_HEIGHT) != 0u) {
        float z = normalize(in_world_pos - view_origin.xyz).z;
        float s = sign(z); z += 0.00001 * (1.0 - s * s);
        float eye = view_origin.z - shadow_heightfog_start.w;
        float pos = in_world_pos.z - shadow_heightfog_start.w;
        float density = (exp(-shadow_fog_params.y * eye) - exp(-shadow_fog_params.y * pos)) /
                        (shadow_fog_params.y * z);
        float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);
        float fraction = clamp((pos - shadow_heightfog_start.w) /
            (shadow_heightfog_end.w - shadow_heightfog_start.w), 0.0, 1.0);
        vec3 color = mix(shadow_heightfog_start.rgb, shadow_heightfog_end.rgb, fraction) * extinction;
        diffuse = mix(diffuse, color, (1.0 - exp(-(shadow_fog_params.x * depth))) * extinction);
    }
}
#endif
void main() {
    out_color = texture(tex_sampler, in_uv);
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u)
        out_color.rgb *= max(shadow_moment_tuning.z, 1.0);
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
    apply_fog(out_color.rgb);
#endif
}
