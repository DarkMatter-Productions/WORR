#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;

layout(std140, set = 1, binding = 0) uniform BloomKernel {
    // x = paired bilinear offset, y = pre-normalized Gaussian pair weight.
    vec4 offset_weight[51];
} bloom_kernel;

layout(push_constant) uniform BloomPush {
    vec4 output_size;
    // x = threshold, y = knee, z = firefly clamp, w = Gaussian sigma.
    vec4 params;
    vec4 aux;
} push_data;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 output_size = max(push_data.output_size.xy, vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;
    vec2 texel = 1.0 / vec2(textureSize(scene_sampler, 0));
#ifdef VK_BLOOM_BLUR_HORIZONTAL
    vec2 direction = vec2(texel.x, 0.0);
#else
    vec2 direction = vec2(0.0, texel.y);
#endif
    float sigma = max(push_data.params.w, 0.5);
    int radius = min(int(sigma * 2.0 + 0.5), 50);
    vec3 sum = vec3(0.0);
    int pair = 0;
    for (int i = -radius; i <= radius; i += 2, ++pair) {
        vec2 tap = bloom_kernel.offset_weight[pair].xy;
        sum += texture(scene_sampler, tc + direction * tap.x).rgb * tap.y;
    }
    out_color = vec4(sum, 1.0);
}
