#version 450

// Alpha-tested BSP casters use the same texture alpha threshold as the
// visible Vulkan world path.  This shader is selected only for material
// subdraws; opaque shadow geometry retains the depth-only fast pipeline.
layout(set = 0, binding = 0) uniform sampler2D tex_sampler;
layout(location = 0) in vec2 in_uv;

#ifdef VK_SHADOW_MOMENT
layout(push_constant) uniform Push {
    mat4 proj;
    mat4 view;
    float filter_mode;
    float evsm_exponent;
    vec2 pad;
} push_data;

layout(location = 0) out vec4 out_moment;
#endif

void main() {
    if (texture(tex_sampler, in_uv).a <= 0.666) {
        discard;
    }

#ifdef VK_SHADOW_MOMENT
    float d = clamp(gl_FragCoord.z, 0.0, 1.0);
    if (int(push_data.filter_mode + 0.5) == 3) {
        float w = exp(min(push_data.evsm_exponent * d, push_data.evsm_exponent));
        out_moment = vec4(w, w * w, 0.0, 1.0);
    } else {
        out_moment = vec4(d, d * d, 0.0, 1.0);
    }
#endif
}
