#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D lut_sampler;
layout(set = 0, binding = 2) uniform sampler2D bloom_sampler;
layout(set = 1, binding = 1) uniform sampler2D auto_exposure_sampler;

// The paired blur weights occupy the first 51 vec4s in the shared per-frame
// post-process UBO. Only the final pass consumes the trailing HDR controls.
layout(std140, set = 1, binding = 0) uniform PostControls {
    vec4 offset_weight[51];
    vec4 hdr;
    vec4 auto_exposure;
} post_controls;

layout(push_constant) uniform Push {
    float time;
    float waterwarp;
    float color_enabled;
    float brightness;
    float contrast;
    float saturation;
    vec2 output_size;
    vec4 tint;
    vec4 split_params;
    vec4 split_shadow;
    vec4 split_highlight;
    vec4 lut_params;
    vec4 bloom_final;
} push_data;

layout(location = 0) out vec4 out_color;

void main() {
    // A one-pixel render through this same native pipeline records temporal
    // exposure from the float scene's final mip. Keep it before all display
    // transforms so it measures the same linear scene OpenGL uses.
    if (push_data.bloom_final.w < -1.5) {
        vec2 output_size = max(push_data.output_size, vec2(1.0));
        vec2 tc = gl_FragCoord.xy / output_size;
        // Keep this preview equivalent to GL's gl_showbloom branch: show
        // the completed level-zero blur directly and bypass all display
        // transforms. The CRT pass, if selected, remains a later pass.
        out_color = texture(scene_sampler, tc);
        return;
    }
    if (push_data.bloom_final.w < -0.5) {
        ivec2 scene_size = textureSize(scene_sampler, 0);
        float max_mip = max(floor(log2(float(max(scene_size.x, scene_size.y)))),
                            0.0);
        vec3 scene = textureLod(scene_sampler, vec2(0.5), max_mip).rgb;
        float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));
        luma = clamp(luma, post_controls.auto_exposure.y,
                     post_controls.auto_exposure.z);
        float target = post_controls.hdr.x / max(luma, 1e-4);
        float previous = texture(lut_sampler, vec2(0.5)).r;
        float exposure = mix(previous, target,
                             clamp(post_controls.auto_exposure.w, 0.0, 1.0));
        out_color = vec4(exposure, exposure, exposure, 1.0);
        return;
    }
    vec2 output_size = max(push_data.output_size, vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;
    if (push_data.waterwarp > 0.5) {
        // The copied scene is sampled in framebuffer coordinates, while
        // OpenGL's GLS_WARP_ENABLE evaluates its sine in the source render
        // target's texture orientation. Its top-origin 2D projection also
        // makes the texture-space Y offset point opposite framebuffer Y.
        vec2 warp_tc = vec2(tc.x, 1.0 - tc.y);
        vec2 warp_offset = 0.0025 * sin(
            warp_tc.yx * 31.41592653589793 + push_data.time);
        tc += warp_offset * vec2(1.0, -1.0);
    }

    vec4 color = texture(scene_sampler, tc);
    if (push_data.bloom_final.w > 0.5) {
        float scene_luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color.rgb = mix(vec3(scene_luma), color.rgb,
                        push_data.bloom_final.y);
        vec3 bloom = texture(bloom_sampler, tc).rgb;
        // Keep the OpenGL multi-level bloom contract: each coarser mip adds
        // a wider halo at half the previous weight. The push value is zero
        // when bloom is inactive, otherwise it is the active mip count.
        if (push_data.bloom_final.w > 1.0) {
            vec3 accum = vec3(0.0);
            float weight = 1.0;
            float weight_sum = 0.0;
            for (int i = 0; i < 6; ++i) {
                if (float(i) >= push_data.bloom_final.w) {
                    break;
                }
                accum += textureLod(bloom_sampler, tc, float(i)).rgb * weight;
                weight_sum += weight;
                weight *= 0.5;
            }
            bloom = accum / max(weight_sum, 1e-5);
        }
        float bloom_luma = dot(bloom, vec3(0.2126, 0.7152, 0.0722));
        bloom = mix(vec3(bloom_luma), bloom, push_data.bloom_final.z);
        color.rgb += bloom * push_data.bloom_final.x;
    }
    if (post_controls.hdr.w > 0.5) {
        // Preserve OpenGL's order exactly: input gamma, exposure, ACES
        // curve/white normalization, then output gamma.
        if (post_controls.hdr.z > 1.0) {
            color.rgb = pow(max(color.rgb, vec3(0.0)),
                            vec3(post_controls.hdr.z));
        }
        float exposure = post_controls.hdr.x;
        if (post_controls.auto_exposure.x > 0.5) {
            exposure = texture(auto_exposure_sampler, vec2(0.5)).r;
        }
        color.rgb *= exposure;
        const float a = 2.51;
        const float b = 0.03;
        const float c = 2.43;
        const float d = 0.59;
        const float e = 0.14;
        vec3 mapped = clamp((color.rgb * (a * color.rgb + b)) /
                            (color.rgb * (c * color.rgb + d) + e),
                            0.0, 1.0);
        vec3 white_input = vec3(max(post_controls.hdr.y, 1e-4));
        vec3 white = clamp((white_input * (a * white_input + b)) /
                           (white_input * (c * white_input + d) + e),
                           0.0, 1.0);
        color.rgb = mapped / max(white, vec3(1e-4));
        if (post_controls.hdr.z > 1.0) {
            color.rgb = pow(max(color.rgb, vec3(0.0)),
                            vec3(1.0 / post_controls.hdr.z));
        }
    }
    if (push_data.color_enabled > 0.5) {
        // Keep the OpenGL color-correction order: contrast, brightness,
        // saturation, then tint. Alpha remains the copied scene alpha.
        color.rgb = (color.rgb - vec3(0.5)) * push_data.contrast + vec3(0.5);
        color.rgb += push_data.brightness;
        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        color.rgb = mix(vec3(luma), color.rgb, push_data.saturation);
        color.rgb *= push_data.tint.rgb;
    }
    if (push_data.split_params.x > 0.0) {
        // Match OpenGL's postfx split-toning order after colour correction.
        float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
        float balance = clamp(push_data.split_params.y, -1.0, 1.0);
        float pivot = 0.5 + balance * 0.5;
        float weight = smoothstep(pivot - 0.25, pivot + 0.25, luma);
        vec3 toned = mix(color.rgb * push_data.split_shadow.rgb,
                         color.rgb * push_data.split_highlight.rgb, weight);
        color.rgb = mix(color.rgb, toned, push_data.split_params.x);
    }
    if (push_data.lut_params.x > 0.0 && push_data.lut_params.y > 1.0) {
        // Match OpenGL's 2D NxN LUT strip interpolation after split toning.
        vec3 lut_color = clamp(color.rgb, 0.0, 1.0);
        float size = push_data.lut_params.y;
        float slice = lut_color.b * (size - 1.0);
        float slice0 = floor(slice);
        float slice1 = min(slice0 + 1.0, size - 1.0);
        float t = slice - slice0;
        // These are texel-space coordinates within a single NxN slice.
        // Divide only after adding the slice origin, matching OpenGL's
        // post-process shader for both horizontal and vertical LUT strips.
        float u = lut_color.r * (size - 1.0) + 0.5;
        float v = lut_color.g * (size - 1.0) + 0.5;
        vec2 uv0;
        vec2 uv1;
        if (push_data.lut_params.z < push_data.lut_params.w) {
            uv0 = vec2((slice0 * size + u) * push_data.lut_params.z,
                       v * push_data.lut_params.w);
            uv1 = vec2((slice1 * size + u) * push_data.lut_params.z,
                       v * push_data.lut_params.w);
        } else {
            uv0 = vec2(u * push_data.lut_params.z,
                       (slice0 * size + v) * push_data.lut_params.w);
            uv1 = vec2(u * push_data.lut_params.z,
                       (slice1 * size + v) * push_data.lut_params.w);
        }
        vec3 graded = mix(texture(lut_sampler, uv0).rgb,
                           texture(lut_sampler, uv1).rgb, t);
        color.rgb = mix(color.rgb, graded, push_data.lut_params.x);
    }
    out_color = color;
}
