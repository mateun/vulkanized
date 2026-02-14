#version 450

/* Bloom composite — combines scene with bloom, then applies:
 *   1. Additive bloom
 *   2. Reinhard tonemapping
 *   3. Scanlines
 *   4. Chromatic aberration
 *   5. Vignette */

layout(location = 0) in  vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_tex;
layout(set = 0, binding = 1) uniform sampler2D bloom_tex;

layout(push_constant) uniform PushConstants {
    float intensity;          /* bloom mix strength */
    float scanline_strength;  /* 0 = off */
    float scanline_count;     /* lines across screen height */
    float aberration;         /* chromatic offset in pixels */
    vec2  screen_size;        /* width, height in pixels */
} pc;

/* Reinhard tone mapping */
vec3 tonemap(vec3 color) {
    return color / (color + vec3(1.0));
}

void main() {
    vec2 uv = frag_uv;

    /* --- Chromatic aberration --- */
    vec2 center_offset = uv - 0.5;
    float dist = length(center_offset);
    vec2 dir = center_offset / (dist + 0.0001);
    vec2 aberration_offset = dir * pc.aberration / pc.screen_size;

    float r = texture(scene_tex, uv + aberration_offset).r;
    float g = texture(scene_tex, uv).g;
    float b = texture(scene_tex, uv - aberration_offset).b;
    vec3 scene_color = vec3(r, g, b);

    /* --- Additive bloom --- */
    vec3 bloom_color = texture(bloom_tex, uv).rgb;
    vec3 color = scene_color + bloom_color * pc.intensity;

    /* --- Reinhard tonemap (HDR -> LDR) --- */
    color = tonemap(color);

    /* --- Scanlines --- */
    if (pc.scanline_strength > 0.0) {
        float scan = sin(uv.y * pc.scanline_count * 3.14159265) * 0.5 + 0.5;
        scan = pow(scan, 0.6); /* soften the scanlines a bit */
        color *= mix(1.0, scan, pc.scanline_strength);
    }

    /* --- Vignette --- */
    float vignette = 1.0 - dot(center_offset, center_offset) * 1.5;
    vignette = clamp(vignette, 0.0, 1.0);
    vignette = pow(vignette, 0.4); /* gentle falloff */
    color *= vignette;

    out_color = vec4(color, 1.0);
}
