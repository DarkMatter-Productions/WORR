#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D emission_sampler;

layout(push_constant) uniform BloomPush {
    vec4 output_size;
    // x = threshold, y = knee, z = firefly clamp, w = Gaussian sigma.
    vec4 params;
    // x is retained for the common native bloom-pass push-constant ABI;
    // y selects the authored-emission descriptor rather than its fallback.
    vec4 aux;
} push_data;

layout(location = 0) out vec4 out_color;

void main() {
    vec2 output_size = max(push_data.output_size.xy, vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;
    vec2 offset = vec2(0.25) / output_size;
    vec3 scene = vec3(0.0);
    scene += texture(scene_sampler, tc + vec2(-offset.x, -offset.y)).rgb;
    scene += texture(scene_sampler, tc + vec2(-offset.x, offset.y)).rgb;
    scene += texture(scene_sampler, tc + vec2(offset.x, -offset.y)).rgb;
    scene += texture(scene_sampler, tc + vec2(offset.x, offset.y)).rgb;
    scene *= 0.25;

    float luma = dot(scene, vec3(0.2126, 0.7152, 0.0722));
    float firefly = push_data.params.z;
    if (firefly > 0.0 && luma > firefly) {
        scene *= firefly / max(luma, 1e-5);
        luma = firefly;
    }
    float threshold = max(push_data.params.x, 0.0);
    float knee = threshold * push_data.params.y + 1e-5;
    float soft = clamp(luma - threshold + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 1e-5);
    float contribution = max(luma - threshold, 0.0) + soft;
    vec3 emission = vec3(0.0);
    if (push_data.aux.y > 0.5) {
        emission += texture(emission_sampler, tc + vec2(-offset.x, -offset.y)).rgb;
        emission += texture(emission_sampler, tc + vec2(-offset.x, offset.y)).rgb;
        emission += texture(emission_sampler, tc + vec2(offset.x, -offset.y)).rgb;
        emission += texture(emission_sampler, tc + vec2(offset.x, offset.y)).rgb;
        emission *= 0.25;
    }
    out_color = vec4(scene * (contribution / max(luma, 1e-5)) + emission, 1.0);
}
