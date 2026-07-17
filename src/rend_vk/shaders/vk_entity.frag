#version 450

#define VK_ENTITY_VERTEX_FULLBRIGHT 1u
#define VK_ENTITY_VERTEX_ALPHATEST 2u
#define VK_ENTITY_VERTEX_LIGHTMAP 4u
#define VK_ENTITY_VERTEX_NO_SHADOW 8u
#define VK_ENTITY_VERTEX_NO_DLIGHT 16u
#define VK_ENTITY_VERTEX_INTENSITY 32u
#define VK_ENTITY_VERTEX_DEFAULT_FLARE 64u
#define VK_ENTITY_VERTEX_RIMLIGHT 128u
#define VK_ENTITY_VERTEX_ITEM_COLORIZE 256u
#define VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE 512u
#define VK_ENTITY_VERTEX_GLOWMAP 1024u
#define VK_ENTITY_VERTEX_NO_FOG 2048u
#define VK_ENTITY_VERTEX_BLOOM_SHELL 4096u
#define VK_ENTITY_VERTEX_BLOOM_RIM 8192u
#define VK_ENTITY_VERTEX_BLOOM_DEPTHHACK 16384u
#define VK_ENTITY_VERTEX_TEXTURE_REPLACE 65536u
#define VK_FOG_GLOBAL 1u
#define VK_FOG_HEIGHT 2u
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
#ifdef VK_ENTITY_BLOOM_EXTRACT_DEPTH_SAMPLE
layout(set = 1, binding = 2) uniform sampler2D bloom_depth_sampler;
#endif
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

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec2 in_lm_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) flat in uint in_flags;
layout(location = 4) in vec3 in_world_pos;
layout(location = 5) in vec3 in_normal;

layout(location = 0) out vec4 out_color;

float shadow_raw_depth(int page, vec2 uv) {
    return texture(shadow_sampler, vec3(uv, float(page))).r;
}

float shadow_compare_depth(int page, vec2 uv, float depth) {
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
    if (kind < 0.5 || shadow_global.x <= 0.0 ||
        (in_flags & VK_ENTITY_VERTEX_NO_SHADOW) != 0u) {
        return 1.0;
    }

    float page = kind < 1.5
        ? shadow_point_page(light, world_pos - light.position_radius.xyz)
        : light.shadow_pages0.x;
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

void apply_fog(inout vec3 diffuse, bool no_fog) {
    if (no_fog) {
        return;
    }
    uint fog_flags = uint(shadow_fog_params.w + 0.5);
    float frag_depth = gl_FragCoord.z / max(gl_FragCoord.w, 1e-6);
    if ((fog_flags & VK_FOG_GLOBAL) != 0u) {
        float d = shadow_fog_color_density.a * frag_depth;
        float fog = 1.0 - exp(-(d * d));
        diffuse = mix(diffuse, shadow_fog_color_density.rgb, fog);
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

vec3 safe_normal(vec3 normal) {
    float len2 = dot(normal, normal);
    if (len2 <= 0.000001) {
        return vec3(0.0, 0.0, 1.0);
    }
    return normal * inversesqrt(len2);
}

#ifdef VK_ENTITY_GPU_BMODEL_FAST_LIT
// This native specialization is selected only for opaque lightmapped inline
// BSP faces with no glow/alpha/fullbright flags and no active sun or dynamic
// light receiver work.  It preserves the generic lightmap, intensity, and
// fog formula while avoiding its unrelated material and shadow branches.
void main() {
    vec4 base = texture(tex_sampler, in_uv);
    // GPU inline-BSP fast-light batches are accepted only after the native
    // submission path excluded transparent and alpha-tested materials. Keep
    // this shader discard-free so opaque depth testing can reject covered
    // fragments before either material sample executes.

    float lm_modulate = max(shadow_dlight_count.y, 0.0);
    float lm_add = shadow_dlight_count.z;
    vec3 lighting = max((texture(lm_sampler, in_lm_uv).rgb + vec3(lm_add)) *
                        lm_modulate, vec3(0.0));
    out_color = vec4(base.rgb * lighting * in_color.rgb,
                     base.a * in_color.a);
    if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
        out_color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
#ifndef VK_ENTITY_GPU_BMODEL_FAST_LIT_NO_FOG
    apply_fog(out_color.rgb, (in_flags & VK_ENTITY_VERTEX_NO_FOG) != 0u);
#endif
}
#elif defined(VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE)
// Selected only for opaque inline-BSP faces whose OpenGL-equivalent material
// is GLS_TEXTURE_REPLACE. The native submission gate rejects transparent and
// alpha-tested inputs, so this companion is deliberately discard-free and
// preserves only base texture, optional intensity, and surface fog.
void main() {
    out_color = texture(tex_sampler, in_uv);
    if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
        out_color.rgb *= max(shadow_moment_tuning.z, 1.0);
    }
#ifndef VK_ENTITY_GPU_BMODEL_TEXTURE_REPLACE_NO_FOG
    apply_fog(out_color.rgb, false);
#endif
}
#else
void main() {
    vec4 base = texture(tex_sampler, in_uv);
    if (base.a < 0.01 ||
        ((in_flags & VK_ENTITY_VERTEX_ALPHATEST) != 0u && base.a < 0.666)) {
        discard;
    }

    // Alias meshes keep OpenGL's default back-face culling.  The native
    // entity pipeline is intentionally two-sided for sprites, particles, and
    // other dynamic primitives, so apply the equivalent cull only to alias
    // shell/rim batches. Otherwise their depth-write-disabled blend paths
    // accept hidden back-facing triangles a second time.
    if ((in_flags & (VK_ENTITY_VERTEX_BLOOM_SHELL |
                     VK_ENTITY_VERTEX_BLOOM_RIM)) != 0u && !gl_FrontFacing) {
        discard;
    }

    if ((in_flags & VK_ENTITY_VERTEX_DEFAULT_FLARE) != 0u) {
        base.rgb *= (base.r + base.g + base.b) / 3.0;
        base.rgb *= in_color.a;
    }

    float texture_intensity = max(shadow_moment_tuning.z, 1.0);
#ifdef VK_ENTITY_BLOOM_EXTRACT
    // The OpenGL bloom MRT records skin glow RGB directly, while lightmapped
    // inline-BSP entity faces record their base texture multiplied by glow
    // alpha. Keep this separate from the lit entity color so scene threshold
    // extraction cannot erase authored emission.
    vec3 emission = vec3(0.0);
    float emission_alpha = base.a * in_color.a;
    if ((in_flags & VK_ENTITY_VERTEX_BLOOM_RIM) != 0u) {
#ifdef VK_ENTITY_BLOOM_EXTRACT_DEPTH_SAMPLE
        // The normal scene pass has already selected the nearest surface at
        // this exact pixel. Sample it instead of replaying equality through a
        // depth attachment: several depth formats quantize alias replays just
        // enough for LESS_OR_EQUAL to become unreliable.
        float scene_depth = texelFetch(bloom_depth_sampler,
                                       ivec2(gl_FragCoord.xy), 0).r;
        const float equal_depth_epsilon = 0.02;
        if (gl_FragCoord.z > scene_depth + equal_depth_epsilon) {
            discard;
        }
#endif
        // GLS_BLOOM_SHELL takes the rim-light diffuse result. The additive
        // extraction pipeline applies its source-alpha blend state, matching
        // the normal OpenGL rim material and avoiding a premultiplied source.
        vec3 view_dir = normalize(view_origin.xyz - in_world_pos);
        float rim = 1.0 - max(dot(safe_normal(in_normal), view_dir), 0.0);
        rim *= rim;
        // The native blur kernel retains more energy than the OpenGL
        // downsample path at the same public bloom settings. Normalize this
        // source so the final rim halo matches the OpenGL receiver response.
        emission = in_color.rgb * rim * 0.70;
        emission_alpha = in_color.a * rim;
    } else if ((in_flags & VK_ENTITY_VERTEX_BLOOM_SHELL) != 0u) {
        // GLS_BLOOM_SHELL assigns bloom = diffuse after the normal
        // GLS_INTENSITY_ENABLE operation.  The extraction pipeline retains
        // alpha blending, so the shell's translucent scene/bloom contract is
        // reproduced by the attachment blend state rather than premultiplying
        // this value here.
        emission = base.rgb * in_color.rgb;
        if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
            emission *= texture_intensity;
        }
        // OpenGL's depth-hacked view weapon follows a distinct effective MRT
        // response from world shells. Preserve that observed receiver energy
        // without perturbing either its visible material or world-shell bloom.
        if ((in_flags & VK_ENTITY_VERTEX_BLOOM_DEPTHHACK) != 0u) {
            emission *= 0.10;
        }
    } else if ((in_flags & VK_ENTITY_VERTEX_GLOWMAP) != 0u) {
        if ((in_flags & VK_ENTITY_VERTEX_LIGHTMAP) != 0u) {
            float glow_alpha = texture(glow_sampler, in_uv).a;
            if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
                glow_alpha *= texture_intensity * shadow_glowmap_tuning.x;
            }
            emission = base.rgb * glow_alpha;
        } else {
            emission = texture(glow_sampler, in_uv).rgb;
            if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
                emission *= texture_intensity * shadow_glowmap_tuning.x;
            }
        }
    }
    if ((in_flags & VK_ENTITY_VERTEX_NO_FOG) == 0u) {
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
            float fog = (1.0 - exp(-(shadow_fog_params.x * frag_depth))) *
                extinction;
            emission *= 1.0 - fog;
        }
    }
    out_color = vec4(emission, emission_alpha);
    return;
#endif

    // GLS_TEXTURE_REPLACE takes precedence over all entity-lighting inputs
    // for opaque inline-BSP faces without a lightmap. In particular, do not
    // turn the fallback light sample into a brightness gain when gl_modulate
    // is above one.
    if ((in_flags & VK_ENTITY_VERTEX_TEXTURE_REPLACE) != 0u) {
        out_color = base;
        if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
            out_color.rgb *= texture_intensity;
        }
        apply_fog(out_color.rgb, (in_flags & VK_ENTITY_VERTEX_NO_FOG) != 0u);
        return;
    }

    vec3 glow_emission = vec3(0.0);
    if ((in_flags & (VK_ENTITY_VERTEX_LIGHTMAP |
                     VK_ENTITY_VERTEX_GLOWMAP)) == VK_ENTITY_VERTEX_GLOWMAP) {
        glow_emission = texture(glow_sampler, in_uv).rgb;
        if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
            glow_emission *= texture_intensity * shadow_glowmap_tuning.x;
        }
    }
    if ((in_flags & (VK_ENTITY_VERTEX_ITEM_COLORIZE |
                     VK_ENTITY_VERTEX_ITEM_COLORIZE_BASE)) != 0u) {
        if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
            base.rgb *= texture_intensity;
        }
        if ((in_flags & VK_ENTITY_VERTEX_ITEM_COLORIZE) != 0u) {
            float lum = dot(base.rgb, vec3(0.299, 0.587, 0.114));
            out_color = vec4(lum * in_color.rgb, base.a * in_color.a);
        } else {
            // The base pass intentionally ignores entity lighting, tint and
            // alpha, matching GLS_ITEM_COLORIZE_BASE.
            out_color = base;
        }
        out_color.rgb += glow_emission;
        apply_fog(out_color.rgb, (in_flags & VK_ENTITY_VERTEX_NO_FOG) != 0u);
        return;
    }

    if ((in_flags & VK_ENTITY_VERTEX_RIMLIGHT) != 0u) {
        vec3 view_dir = normalize(view_origin.xyz - in_world_pos);
        float rim = 1.0 - max(dot(safe_normal(in_normal), view_dir), 0.0);
        rim *= rim;
        out_color = vec4(in_color.rgb * rim, in_color.a * rim);
        apply_fog(out_color.rgb, (in_flags & VK_ENTITY_VERTEX_NO_FOG) != 0u);
        return;
    }

    vec3 normal = safe_normal(in_normal);
    bool fullbright = (in_flags & VK_ENTITY_VERTEX_FULLBRIGHT) != 0u;
    bool lightmapped = (in_flags & VK_ENTITY_VERTEX_LIGHTMAP) != 0u;
    // shadow_dlight_count.y/z/w carry gl_modulate*gl_modulate_world,
    // gl_brightness, and gl_modulate*gl_modulate_entities.
    float lm_modulate = max(shadow_dlight_count.y, 0.0);
    float lm_add = shadow_dlight_count.z;
    float entity_modulate = max(shadow_dlight_count.w, 0.0);
    vec3 lighting = in_color.rgb;
    vec3 modulation = vec3(1.0);
    if (!fullbright) {
        if (lightmapped) {
            // Inline BSP faces follow the world formula:
            // (lm * sun + dlights + add) * modulate.
            lighting = texture(lm_sampler, in_lm_uv).rgb;
            if ((in_flags & VK_ENTITY_VERTEX_GLOWMAP) != 0u) {
                float glow_scale = shadow_glowmap_tuning.x;
                if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
                    glow_scale *= texture_intensity;
                }
                lighting = mix(lighting, vec3(1.0),
                               texture(glow_sampler, in_uv).a * glow_scale);
            }
            modulation = in_color.rgb;
            if ((in_flags & VK_ENTITY_VERTEX_NO_SHADOW) == 0u) {
                lighting *= shadow_sun_factor(in_world_pos, normal);
            }
            if ((in_flags & VK_ENTITY_VERTEX_NO_DLIGHT) == 0u) {
                lighting += calc_dynamic_lights(in_world_pos, normal);
            }
            lighting = max((lighting + vec3(lm_add)) * lm_modulate, vec3(0.0));
        } else {
            // CPU-lit models carry unmodulated static light in in_color; the
            // entity modulate applies here so values above 1.0 survive the
            // UNORM vertex encoding. Per-pixel dynamic lights gain the world
            // modulate like the GL mesh path.
            lighting *= entity_modulate;
            if ((in_flags & VK_ENTITY_VERTEX_NO_SHADOW) == 0u) {
                lighting *= shadow_sun_factor(in_world_pos, normal);
            }
            if ((in_flags & VK_ENTITY_VERTEX_NO_DLIGHT) == 0u) {
                lighting += calc_dynamic_lights(in_world_pos, normal) * lm_modulate;
            }
        }
    } else {
        lighting = vec3(1.0);
        modulation = in_color.rgb;
    }

    out_color = vec4(base.rgb * lighting * modulation,
                     base.a * in_color.a);
    if ((in_flags & VK_ENTITY_VERTEX_INTENSITY) != 0u) {
        out_color.rgb *= texture_intensity;
    }
    // Skin glow maps are premultiplied during registration and are added
    // after the lit base, just as the OpenGL GLS_GLOWMAP_ENABLE path does.
    out_color.rgb += glow_emission;
    apply_fog(out_color.rgb, (in_flags & VK_ENTITY_VERTEX_NO_FOG) != 0u);
}
#endif
