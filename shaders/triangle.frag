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
    if (pc.use_texture != 0u) {
        vec4 tex_sample = texture(tex, frag_uv);
        color *= tex_sample.rgb;
    }
    out_color = vec4(color, 1.0);
}
