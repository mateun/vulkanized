#version 450

layout(location = 0) in vec2 in_position; /* screen-space pixels */
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

layout(push_constant) uniform PushConstants {
    vec2 screen_size; /* width, height in pixels */
} pc;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec3 frag_color;

void main() {
    /* Convert pixel coordinates to NDC: [0,W] -> [-1,1], [0,H] -> [-1,1] */
    vec2 ndc = (in_position / pc.screen_size) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
    frag_uv    = in_uv;
    frag_color = in_color;
}
