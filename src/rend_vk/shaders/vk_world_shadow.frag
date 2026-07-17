#version 450

#define VK_WORLD_VERTEX_FULLBRIGHT 2u
#define VK_WORLD_VERTEX_ALPHATEST 4u
#define VK_WORLD_VERTEX_LIGHTMAPPED 16u
#define VK_WORLD_VERTEX_INTENSITY 32u
#define VK_WORLD_VERTEX_GLOWMAP 64u
#define VK_WORLD_VERTEX_SKY 128u
#define VK_FOG_GLOBAL 1u
#define VK_FOG_HEIGHT 2u
#define VK_FOG_SKY 4u
#define SHADOW_VISIBILITY_EXPONENT 2.0
#define VK_SHADOW_MAX_PAGES 64
#define VK_SHADOW_MAX_DLIGHTS 64
#define DLIGHT_CUTOFF 64.0

struct shadow_page_t {
    mat4 matrix;
    vec4 params;
};

struct shadow_dlight_t {
    vec4 position_radius;
    vec4 color_intensity;
    vec4 cone;
    vec4 shadow_pages0;
    vec4 shadow_pages1;
};

layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(set = 1, binding = 0) uniform sampler2D lm_sampler;
layout(set = 2, binding = 0) uniform sampler2DArray shadow_sampler;
layout(set = 2, binding = 3) uniform sampler2DArrayShadow shadow_sampler_cmp;
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
    shadow_page_t shadow_pages[VK_SHADOW_MAX_PAGES];
    shadow_dlight_t shadow_dlights[VK_SHADOW_MAX_DLIGHTS];
};
layout(set = 2, binding = 2) uniform sampler2DArray shadow_moments;
layout(set = 0, binding = 1) uniform sampler2D glow_sampler;
#ifdef VK_WORLD_ANIMATED
layout(set = 3, binding = 0) uniform sampler2D refract_sampler;
#endif

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_lm_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) flat in uint in_flags;
layout(location = 4) in vec3 in_world_pos;
layout(location = 5) in vec3 in_normal;
#ifdef VK_WORLD_ANIMATED
layout(location = 6) flat in float in_time;
layout(location = 7) flat in float in_refraction_scale;
layout(location = 8) flat in uint in_effects_enabled;
layout(location = 9) flat in uint in_refraction_enabled;
#endif

layout(location = 0) out vec4 out_color;

float shadow_raw_depth(int page, vec2 uv) {
    return texture(shadow_sampler, vec3(uv, float(page))).r;
}

float shadow_compare_depth(int page, vec2 uv, float depth) {
    // Use hardware depth compare for hard/PCF paths; keep raw depth sampling
    // available for PCSS blocker search and moment filters.
    return texture(shadow_sampler_cmp, vec4(uv, float(page), depth));
}

float shadow_pcf_depth(int page, vec3 tc, float bias, float radius_texels) {
    float depth = tc.z - bias;
    float inv_res = shadow_pages[page].params.y;
    float result = 0.0;
    float count = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 ofs = vec2(float(x), float(y)) * inv_res * radius_texels;
            result += shadow_compare_depth(page, tc.xy + ofs, depth);
            count += 1.0;
        }
    }
    return result / max(count, 1.0);
}

float shadow_pcss_depth(int page, vec3 tc, float bias) {
    float receiver = tc.z - bias;
    float inv_res = shadow_pages[page].params.y;
    float blockers = 0.0;
    float blocker_depth = 0.0;
    for (int y = -2; y <= 2; y++) {
        for (int x = -2; x <= 2; x++) {
            vec2 ofs = vec2(float(x), float(y)) * inv_res * 3.0;
            float sample_depth = shadow_raw_depth(page, tc.xy + ofs);
            if (sample_depth + bias < receiver) {
                blocker_depth += sample_depth;
                blockers += 1.0;
            }
        }
    }
    if (blockers <= 0.0) {
        return 1.0;
    }
    blocker_depth /= blockers;
    float penumbra = clamp((receiver - blocker_depth) /
                           max(blocker_depth, 0.01) * 16.0,
                           1.0, 8.0);
    penumbra = min(penumbra * max(shadow_global.z, 0.25), 16.0);
    return shadow_pcf_depth(page, tc, bias, penumbra);
}

float shadow_moment_factor(int page, vec3 tc, float bias, float filter_mode) {
    float depth = clamp(tc.z - bias, 0.0, 1.0);
    vec2 moments;
    float evsm_exponent = max(shadow_moment_tuning.y, 0.0001);
    if (filter_mode > 2.5) {
        depth = exp(min(evsm_exponent * depth, evsm_exponent));
        moments = texture(shadow_moments, vec3(tc.xy, float(page))).xy;
    } else {
        moments = texture(shadow_moments, vec3(tc.xy, float(page))).xy;
    }
    if (depth <= moments.x) {
        return 1.0;
    }
    float min_variance = max(shadow_moment_tuning.x, 0.0);
    float variance = max(moments.y - moments.x * moments.x, min_variance);
    float d = depth - moments.x;
    float p = variance / (variance + d * d);
    return clamp((p - 0.18) / 0.82, 0.0, 1.0);
}

float shadow_sample_page_covered(int page, vec3 world_pos, vec3 normal,
                                 out float covered) {
    covered = 0.0;
    if (page < 0 || page >= int(shadow_global.x)) {
        return 1.0;
    }

    vec3 sample_pos = world_pos + normal * shadow_pages[page].params.w;
    vec4 clip = shadow_pages[page].matrix * vec4(sample_pos, 1.0);
    if (clip.w <= 0.0) {
        return 1.0;
    }

    vec3 tc = (clip.xyz / clip.w) * 0.5 + vec3(0.5);
    if (any(lessThan(tc, vec3(0.0))) || any(greaterThan(tc, vec3(1.0)))) {
        return 1.0;
    }

    covered = min(min(tc.x, 1.0 - tc.x), min(tc.y, 1.0 - tc.y));
    float bias = shadow_pages[page].params.z;
    float filter_mode = shadow_pages[page].params.x;
    float result;
    if (filter_mode < 0.5) {
        result = shadow_compare_depth(page, tc.xy, tc.z - bias);
    } else if (filter_mode < 1.5) {
        result = shadow_pcf_depth(page, tc, bias, max(shadow_global.z, 0.25));
    } else if (filter_mode < 3.5) {
        result = shadow_moment_factor(page, tc, bias, filter_mode);
    } else {
        result = shadow_pcss_depth(page, tc, bias);
    }
    result = pow(clamp(result, 0.0, 1.0), SHADOW_VISIBILITY_EXPONENT);
    return mix(1.0, result, clamp(shadow_global.y, 0.0, 1.0));
}

float shadow_sun_factor(vec3 world_pos, vec3 normal) {
    if (shadow_sun.x < 0.0 || shadow_sun.y <= 0.0) {
        return 1.0;
    }
    int first_page = int(shadow_sun.x + 0.5);
    int page_count = int(shadow_sun.y + 0.5);
    for (int i = 0; i < 4; i++) {
        if (i >= page_count) {
            break;
        }
        float covered = 0.0;
        float factor = shadow_sample_page_covered(first_page + i, world_pos,
                                                  normal, covered);
        if (covered > 0.12 || i + 1 >= page_count) {
            return factor;
        }
        float next_covered = 0.0;
        float next_factor =
            shadow_sample_page_covered(first_page + i + 1, world_pos, normal,
                                       next_covered);
        if (next_covered > 0.0) {
            return mix(next_factor, factor, smoothstep(0.0, 0.12, covered));
        }
    }
    return 1.0;
}

float shadow_sample_page(int page, vec3 world_pos, vec3 normal) {
    float covered = 0.0;
    return shadow_sample_page_covered(page, world_pos, normal, covered);
}

float shadow_point_page(shadow_dlight_t light, vec3 light_to_frag) {
    vec3 adir = abs(light_to_frag);
    int face;
    if (adir.x >= adir.y && adir.x >= adir.z) {
        face = light_to_frag.x >= 0.0 ? 0 : 1;
    } else if (adir.y >= adir.z) {
        face = light_to_frag.y >= 0.0 ? 2 : 3;
    } else {
        face = light_to_frag.z >= 0.0 ? 4 : 5;
    }
    return face < 4 ? light.shadow_pages0[face] : light.shadow_pages1[face - 4];
}

float shadow_factor_for_light(shadow_dlight_t light, vec3 world_pos,
                              vec3 normal) {
    float kind = light.shadow_pages1.z;
    if (kind < 0.5 || shadow_global.x <= 0.0) {
        return 1.0;
    }

    float page = kind < 1.5
        ? shadow_point_page(light, world_pos - light.position_radius.xyz)
        : light.shadow_pages0.x;
    // int() truncates toward zero, so the -1 "no page" sentinel would
    // otherwise alias to page 0.
    if (page < 0.0) {
        return 1.0;
    }
    return shadow_sample_page(int(page + 0.5), world_pos, normal);
}

vec3 calc_dynamic_lights(vec3 world_pos, vec3 normal) {
    vec3 shade = vec3(0.0);
    int count = int(shadow_dlight_count.x + 0.5);

    for (int i = 0; i < VK_SHADOW_MAX_DLIGHTS; i++) {
        if (i >= count) {
            break;
        }

        shadow_dlight_t light = shadow_dlights[i];
        vec3 light_dir = light.position_radius.xyz - world_pos;
        float dist = length(light_dir);
        float radius = light.position_radius.w + DLIGHT_CUTOFF;
        float atten = max(radius - dist - DLIGHT_CUTOFF, 0.0) / radius;
        if (atten <= 0.0) {
            continue;
        }

        vec3 dir = light_dir / max(dist, 1.0);
        float lambert;
        if (light.color_intensity.r < 0.0) {
            lambert = 1.0;
        } else {
            lambert = max(dot(normal, dir), 0.0);
            if (lambert <= 0.0) {
                continue;
            }
        }

        vec3 result = light.color_intensity.rgb * light.color_intensity.a *
            atten * lambert;

        if (light.cone.w != 0.0) {
            float mag = -dot(dir, light.cone.xyz);
            float cone_cos = clamp(light.cone.w, -1.0, 0.9999);
            float spot = clamp((mag - cone_cos) /
                               max(1.0 - cone_cos, 0.0001), 0.0, 1.0);
            if (spot <= 0.0) {
                continue;
            }
            result *= spot;
        }

        result *= shadow_factor_for_light(light, world_pos, normal);
        shade += result;
    }

    return shade;
}

void apply_fog(inout vec3 diffuse, bool sky) {
    uint fog_flags = uint(shadow_fog_params.w + 0.5);
    if (sky) {
        if ((fog_flags & VK_FOG_SKY) != 0u) {
            diffuse = mix(diffuse, shadow_fog_color_density.rgb,
                          shadow_fog_params.z);
        }
        return;
    }

    float frag_depth = gl_FragCoord.z / max(gl_FragCoord.w, 1e-6);
    if ((fog_flags & VK_FOG_GLOBAL) != 0u) {
        float d = shadow_fog_color_density.a * frag_depth;
        float fog = 1.0 - exp(-(d * d));
        diffuse = mix(diffuse, shadow_fog_color_density.rgb, fog);
    }
    if ((fog_flags & VK_FOG_HEIGHT) != 0u) {
        // Preserve the rerelease height-fog equation, including its
        // directional epsilon, while evaluating from native world data.
        float dir_z = normalize(in_world_pos - view_origin.xyz).z;
        float s = sign(dir_z);
        dir_z += 0.00001 * (1.0 - s * s);
        float eye = view_origin.z - shadow_heightfog_start.w;
        float pos = in_world_pos.z - shadow_heightfog_start.w;
        float density =
            (exp(-shadow_fog_params.y * eye) -
             exp(-shadow_fog_params.y * pos)) /
            (shadow_fog_params.y * dir_z);
        float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);
        float fraction = clamp((pos - shadow_heightfog_start.w) /
                                   (shadow_heightfog_end.w -
                                    shadow_heightfog_start.w),
                               0.0, 1.0);
        vec3 fog_color = mix(shadow_heightfog_start.rgb,
                             shadow_heightfog_end.rgb, fraction) * extinction;
        float fog = (1.0 - exp(-(shadow_fog_params.x * frag_depth))) * extinction;
        diffuse = mix(diffuse, fog_color, fog);
    }
}

#ifdef VK_WORLD_STATIC_FAST_LIT
// Selected only for ordinary opaque lightmapped world batches while no sun or
// dynamic receiver lighting is active.  Keep the lightmap/intensity/fog
// equation byte-for-byte analogous to the general receiver path without
// material, warp, fullbright, and shadow control flow.
void main() {
    vec4 base = texture(tex_sampler, in_uv);
    vec4 lm = texture(lm_sampler, in_lm_uv);
    vec3 lighting = max((lm.rgb + vec3(shadow_dlight_count.z)) *
                        max(shadow_dlight_count.y, 0.0), vec3(0.0));
    vec4 color = base * vec4(lighting, lm.a) * in_color;
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
        color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
#ifndef VK_WORLD_STATIC_FAST_LIT_NO_FOG
    apply_fog(color.rgb, false);
#endif
    out_color = color;
}
#elif defined(VK_WORLD_STATIC_FAST_LIT_GLOWMAP)
// The glow-map companion for the static-light receiver.  Authored glow maps
// raise the lightmap toward white even when bloom output is disabled, so this
// remains a distinct receiver class instead of treating it as texture-replace.
// It preserves that material equation while omitting unavailable sun/dlight
// receiver work.
void main() {
    vec4 base = texture(tex_sampler, in_uv);
    vec4 lm = texture(lm_sampler, in_lm_uv);
    float glow_scale = shadow_glowmap_tuning.x;
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
        glow_scale *= max(shadow_moment_tuning.z, 1.0);
    }
    lm.rgb = mix(lm.rgb, vec3(1.0), texture(glow_sampler, in_uv).a * glow_scale);
    vec3 lighting = max((lm.rgb + vec3(shadow_dlight_count.z)) *
                        max(shadow_dlight_count.y, 0.0), vec3(0.0));
    vec4 color = base * vec4(lighting, lm.a) * in_color;
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
        color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
#ifndef VK_WORLD_STATIC_FAST_LIT_GLOWMAP_NO_FOG
    apply_fog(color.rgb, false);
#endif
    out_color = color;
}
#elif defined(VK_WORLD_TEXTURE_REPLACE)
// Mirrors the OpenGL GLS_TEXTURE_REPLACE opaque-wall path.  Non-lightmapped
// opaque BSP faces bypass the receiver lighting equation entirely: the base
// texture is authoritative, with only the shared intensity and fog effects.
// This is deliberately independent of sun/dlight state because OpenGL does
// not light texture-replace walls with either source.
void main() {
    vec4 color = texture(tex_sampler, in_uv);
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
        color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
#ifndef VK_WORLD_TEXTURE_REPLACE_NO_FOG
    apply_fog(color.rgb, false);
#endif
    out_color = color;
}
#else
void main() {
    vec2 uv = in_uv;
#ifdef VK_WORLD_ANIMATED
    vec2 warp_ofs = vec2(0.0);
    if (in_effects_enabled != 0u && (in_flags & 1u) != 0u) {
        // Keep turbulent warp per-fragment, as in the OpenGL shader. Doing
        // this in the vertex stage flattens the sine wave over large faces.
        warp_ofs = 0.0625 * sin(uv.yx * 4.0 + in_time);
        uv += warp_ofs;
    }
#endif
    vec4 base = texture(tex_sampler, uv);
    if ((in_flags & VK_WORLD_VERTEX_ALPHATEST) != 0u && base.a <= 0.666) {
        discard;
    }

#ifdef VK_WORLD_BLOOM_EXTRACT
    // OpenGL's bloom MRT stores authored wall emission separately from the
    // ordinary scene: diffuse RGB multiplied by the glow-map alpha, before
    // lightmaps, dynamic lighting, modulation, or scene thresholding.  Keep
    // that contract in a bloom-only native pass so the standard scene pass
    // never pays for another color attachment.
    float glow_alpha = 0.0;
    if ((in_flags & VK_WORLD_VERTEX_GLOWMAP) != 0u) {
        glow_alpha = texture(glow_sampler, uv).a;
        if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
            glow_alpha *= max(shadow_moment_tuning.z, 1.0);
        }
    }
    vec3 emission = base.rgb * glow_alpha;
    uint fog_flags = uint(shadow_fog_params.w + 0.5);
    float frag_depth = gl_FragCoord.z / max(gl_FragCoord.w, 1e-6);
    if ((fog_flags & VK_FOG_GLOBAL) != 0u) {
        float d = shadow_fog_color_density.a * frag_depth;
        emission *= exp(-(d * d));
    }
    if ((fog_flags & VK_FOG_HEIGHT) != 0u) {
        float dir_z = normalize(in_world_pos - view_origin.xyz).z;
        float s = sign(dir_z);
        dir_z += 0.00001 * (1.0 - s * s);
        float eye = view_origin.z - shadow_heightfog_start.w;
        float pos = in_world_pos.z - shadow_heightfog_start.w;
        float density =
            (exp(-shadow_fog_params.y * eye) -
             exp(-shadow_fog_params.y * pos)) /
            (shadow_fog_params.y * dir_z);
        float extinction = 1.0 - clamp(exp(-density), 0.0, 1.0);
        float fog = (1.0 - exp(-(shadow_fog_params.x * frag_depth))) * extinction;
        emission *= 1.0 - fog;
    }
    out_color = vec4(emission, base.a);
    return;
#endif

    vec4 lm = texture(lm_sampler, in_lm_uv);
    if ((in_flags & (VK_WORLD_VERTEX_LIGHTMAPPED |
                     VK_WORLD_VERTEX_GLOWMAP)) ==
        (VK_WORLD_VERTEX_LIGHTMAPPED | VK_WORLD_VERTEX_GLOWMAP)) {
        // GL's wall glow map uses alpha, not RGB, to raise the authored
        // lightmap toward white before dynamic lighting/modulate is applied.
        float glow_scale = shadow_glowmap_tuning.x;
        if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
            glow_scale *= max(shadow_moment_tuning.z, 1.0);
        }
        lm.rgb = mix(lm.rgb, vec3(1.0), texture(glow_sampler, uv).a * glow_scale);
    }
    vec3 normal = normalize(in_normal);
    // shadow_moment_tuning.w carries the global r_fullbright state. Keep the
    // authored per-surface fullbright flag in the same branch so runtime cvar
    // changes do not require rebuilding or re-uploading the static world.
    bool fullbright = shadow_moment_tuning.w > 0.5 ||
                      (in_flags & VK_WORLD_VERTEX_FULLBRIGHT) != 0u;
    vec3 lighting = fullbright ? vec3(1.0) : lm.rgb;
    // Match GL: shadows, dynamic lights, brightness add and modulate apply
    // only to lightmapped surfaces; unlit faces (classic warp water) keep
    // their raw texture brightness. shadow_dlight_count.y/z carry
    // gl_modulate*gl_modulate_world and gl_brightness.
    if (!fullbright && (in_flags & VK_WORLD_VERTEX_LIGHTMAPPED) != 0u) {
        lighting *= shadow_sun_factor(in_world_pos, normal);
        lighting += calc_dynamic_lights(in_world_pos, normal);
        lighting = max((lighting + vec3(shadow_dlight_count.z)) *
                       max(shadow_dlight_count.y, 0.0), vec3(0.0));
    }
    vec4 color = base * vec4(lighting, lm.a) * in_color;
    if ((in_flags & VK_WORLD_VERTEX_INTENSITY) != 0u) {
        color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
    apply_fog(color.rgb, (in_flags & VK_WORLD_VERTEX_SKY) != 0u);
#ifdef VK_WORLD_ANIMATED
    // Match the OpenGL refract path: sample the completed opaque scene using
    // the turbulent offset in screen pixels, then compensate for the regular
    // source-alpha blend performed by the graphics pipeline.
    if (in_refraction_enabled != 0u && in_refraction_scale > 0.0 &&
        (in_flags & 1u) != 0u) {
        vec2 base_tc = gl_FragCoord.xy / vec2(textureSize(refract_sampler, 0));
        vec2 refr_tc = base_tc;
        vec2 warp_tc = warp_ofs * in_refraction_scale;
        vec2 dxtc = dFdx(in_uv);
        vec2 dytc = dFdy(in_uv);
        float det = dxtc.x * dytc.y - dxtc.y * dytc.x;
        if (abs(det) > 1e-6) {
            vec2 pix_ofs = vec2((dytc.y * warp_tc.x - dxtc.y * warp_tc.y),
                                (-dytc.x * warp_tc.x + dxtc.x * warp_tc.y)) / det;
            refr_tc = base_tc + pix_ofs / vec2(textureSize(refract_sampler, 0));
        } else {
            refr_tc = base_tc + warp_tc;
        }
        vec3 scene_base = texture(refract_sampler, base_tc).rgb;
        vec3 scene_refracted = texture(refract_sampler, refr_tc).rgb;
        float alpha = clamp(color.a, 0.0, 1.0);
        vec3 desired = mix(scene_refracted, color.rgb, alpha);
        color.rgb = alpha > 0.0001
            ? clamp((desired - scene_base * (1.0 - alpha)) / alpha, 0.0, 1.0)
            : desired;
    }
#endif
    out_color = color;
}
#endif
