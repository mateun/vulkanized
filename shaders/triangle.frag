#version 450

layout(location = 0) in  vec3 frag_color;
layout(location = 1) in  vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
} pc;

void main() {
    vec3 color = frag_color;
    float alpha = 1.0;

    if (pc.use_texture != 0u) {
        vec4 tex_sample = texture(tex, frag_uv);
        color *= tex_sample.rgb;
        alpha = tex_sample.a;
    }

    /* Discard nearly-transparent fragments so they don't write to the
     * depth buffer and block geometry behind them. */
    if (alpha < 0.01)
        discard;

    out_color = vec4(color, alpha);
}
