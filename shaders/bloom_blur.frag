#version 450

/* 9-tap separable Gaussian blur.
 * Direction is passed as push constant: (1/w, 0) for horizontal, (0, 1/h) for vertical. */

layout(location = 0) in  vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D src_tex;

layout(push_constant) uniform PushConstants {
    vec2 direction; /* texel step: (1/width, 0) or (0, 1/height) */
} pc;

void main() {
    /* 9-tap Gaussian weights (sigma ~2.5, normalized) */
    const float weights[5] = float[](
        0.227027, 0.194596, 0.121622, 0.054054, 0.016216
    );

    vec3 result = texture(src_tex, frag_uv).rgb * weights[0];

    for (int i = 1; i < 5; i++) {
        vec2 offset = pc.direction * float(i);
        result += texture(src_tex, frag_uv + offset).rgb * weights[i];
        result += texture(src_tex, frag_uv - offset).rgb * weights[i];
    }

    out_color = vec4(result, 1.0);
}
