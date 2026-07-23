#version 450

// Native Vulkan CRT presentation pass. It intentionally samples the copied
// completed scene and writes the swapchain, so HUD/menu overlays remain sharp.
layout(set = 0, binding = 0) uniform sampler2D scene_sampler;

layout(push_constant) uniform CrtPush {
    vec4 params;  // hard pixels, hard scanlines, brightness boost, linear gamma
    vec4 params2; // mask dark, mask light, shadow-mask type, scanline scale
    vec4 texel;   // inverse scene width/height, scene width/height
} push_data;

layout(location = 0) out vec4 out_color;

float crt_to_linear_1(float c)
{
    if (push_data.params.w <= 0.0)
        return c;
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 crt_to_linear(vec3 c)
{
    return vec3(crt_to_linear_1(c.r), crt_to_linear_1(c.g),
                crt_to_linear_1(c.b));
}

float crt_to_srgb_1(float c)
{
    if (push_data.params.w <= 0.0)
        return c;
    return c < 0.0031308 ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
}

vec3 crt_to_srgb(vec3 c)
{
    return vec3(crt_to_srgb_1(c.r), crt_to_srgb_1(c.g), crt_to_srgb_1(c.b));
}

float crt_gauss(float x, float scale)
{
    return exp2(scale * pow(abs(x), 2.0));
}

vec2 crt_dist(vec2 pos)
{
    pos *= push_data.texel.zw;
    return -((pos - floor(pos)) - vec2(0.5));
}

vec3 crt_fetch(vec2 pos, vec2 offset)
{
    vec2 tc = (floor(pos * push_data.texel.zw + offset) + vec2(0.5)) *
        push_data.texel.xy;
    return crt_to_linear(texture(scene_sampler, tc).rgb * push_data.params.z);
}

vec3 crt_horz3(vec2 pos, float offset, float hard_pix)
{
    vec3 b = crt_fetch(pos, vec2(-1.0, offset));
    vec3 c = crt_fetch(pos, vec2(0.0, offset));
    vec3 d = crt_fetch(pos, vec2(1.0, offset));
    float distance = crt_dist(pos).x;
    float wb = crt_gauss(distance - 1.0, hard_pix);
    float wc = crt_gauss(distance, hard_pix);
    float wd = crt_gauss(distance + 1.0, hard_pix);
    return (b * wb + c * wc + d * wd) / (wb + wc + wd);
}

vec3 crt_horz5(vec2 pos, float offset, float hard_pix)
{
    vec3 a = crt_fetch(pos, vec2(-2.0, offset));
    vec3 b = crt_fetch(pos, vec2(-1.0, offset));
    vec3 c = crt_fetch(pos, vec2(0.0, offset));
    vec3 d = crt_fetch(pos, vec2(1.0, offset));
    vec3 e = crt_fetch(pos, vec2(2.0, offset));
    float distance = crt_dist(pos).x;
    float wa = crt_gauss(distance - 2.0, hard_pix);
    float wb = crt_gauss(distance - 1.0, hard_pix);
    float wc = crt_gauss(distance, hard_pix);
    float wd = crt_gauss(distance + 1.0, hard_pix);
    float we = crt_gauss(distance + 2.0, hard_pix);
    return (a * wa + b * wb + c * wc + d * wd + e * we) /
        (wa + wb + wc + wd + we);
}

float crt_scan(vec2 pos, float offset, float hard_scan)
{
    return crt_gauss(crt_dist(pos).y + offset, hard_scan);
}

vec3 crt_tri(vec2 pos, float hard_pix, float hard_scan)
{
    vec3 a = crt_horz3(pos, -1.0, hard_pix);
    vec3 b = crt_horz5(pos, 0.0, hard_pix);
    vec3 c = crt_horz3(pos, 1.0, hard_pix);
    float wa = crt_scan(pos, -1.0, hard_scan);
    float wb = crt_scan(pos, 0.0, hard_scan);
    float wc = crt_scan(pos, 1.0, hard_scan);
    return a * wa + b * wb + c * wc;
}

vec3 crt_mask(vec2 pos)
{
    float mask = push_data.params2.z;
    if (mask < 0.5)
        return vec3(1.0);

    float mask_dark = push_data.params2.x;
    float mask_light = push_data.params2.y;
    vec3 output_mask = vec3(mask_dark);
    if (mask < 1.5) {
        float line = mask_light;
        float odd = fract(pos.x * 0.16666666) < 0.5 ? 1.0 : 0.0;
        if (fract((pos.y + odd) * 0.5) < 0.5)
            line = mask_dark;
        pos.x = fract(pos.x * 0.33333333);
        if (pos.x < 0.333)
            output_mask.r = mask_light;
        else if (pos.x < 0.666)
            output_mask.g = mask_light;
        else
            output_mask.b = mask_light;
        return output_mask * line;
    }

    if (mask >= 2.5) {
        if (mask >= 3.5)
            pos = floor(pos * vec2(1.0, 0.5));
        pos.x += pos.y * 3.0;
        pos.x = fract(pos.x * 0.16666666);
    } else {
        pos.x = fract(pos.x * 0.33333333);
    }

    if (pos.x < 0.333)
        output_mask.r = mask_light;
    else if (pos.x < 0.666)
        output_mask.g = mask_light;
    else
        output_mask.b = mask_light;
    return output_mask;
}

float crt_scanline_mod(float hard_scan)
{
    float scan_dark = exp2(hard_scan * 0.25);
    float scale = max(push_data.params2.w, 1.0);
    // The final Vulkan pass uses a negative-height viewport to preserve the
    // engine's top-left presentation convention. Offset by one complete
    // expanded source scanline rather than one fixed output pixel: this keeps
    // both the bright/dark parity and the first row of each scaled block on
    // the same rows as OpenGL at native, half, and quarter resolution.
    float phase = scale;
    float line = mod(floor((gl_FragCoord.y + phase) / scale), 2.0);
    return mix(scan_dark, 1.0, line);
}

void main()
{
    vec2 tc = gl_FragCoord.xy * push_data.texel.xy;
    vec3 color = crt_tri(tc, push_data.params.x, push_data.params.y);
    color *= crt_scanline_mod(push_data.params.y);
    if (push_data.params2.z > 0.0)
        color *= crt_mask(gl_FragCoord.xy * 1.000001);
    out_color = vec4(crt_to_srgb(min(color, vec3(1.0))), 1.0);
}
