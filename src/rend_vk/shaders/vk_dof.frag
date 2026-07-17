#version 450

layout(set = 0, binding = 0) uniform sampler2D scene_sampler;
layout(set = 0, binding = 1) uniform sampler2D blur_sampler;
layout(set = 0, binding = 2) uniform sampler2D depth_sampler;

layout(push_constant) uniform DofPush {
    // x = focus distance, y = blur range, z = transition strength,
    // w = OpenGL-style projection matrix [10].
    vec4 params;
    // x = OpenGL-style projection matrix [14].
    vec4 projection;
    // xy = inclusive rect origin, zw = exclusive rect limit in scene UVs.
    vec4 rect;
} push_data;

layout(location = 0) out vec4 out_color;

float linearize_depth(float depth) {
    float z = depth * 2.0 - 1.0;
    return abs(push_data.projection.x / (z + push_data.params.w));
}

void main() {
    vec2 output_size = max(vec2(textureSize(scene_sampler, 0)), vec2(1.0));
    vec2 tc = gl_FragCoord.xy / output_size;

    vec4 scene = texture(scene_sampler, tc);

    float focus_dist = push_data.params.x;
    if (focus_dist <= 0.0) {
        focus_dist = linearize_depth(texture(depth_sampler, vec2(0.5)).r);
    }
    float blur_range = push_data.params.y;
    if (blur_range <= 0.0) {
        blur_range = max(64.0, focus_dist * 0.25);
    }

    float dist = linearize_depth(texture(depth_sampler, tc).r);
    float blur_factor = clamp(abs(dist - focus_dist) / blur_range, 0.0, 1.0);
    blur_factor = smoothstep(0.0, 1.0, blur_factor);
    blur_factor *= clamp(push_data.params.z, 0.0, 1.0);
    out_color = mix(scene, texture(blur_sampler, tc), blur_factor);
}
