#version 450

/* Brightness extraction — soft-knee threshold.
 * Only passes pixels whose luminance exceeds the threshold,
 * with a smooth transition band controlled by soft_threshold. */

layout(location = 0) in  vec2 frag_uv;
layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D scene_tex;

layout(push_constant) uniform PushConstants {
    float threshold;
    float soft_threshold;
} pc;

void main() {
    vec3 color = texture(scene_tex, frag_uv).rgb;

    /* Perceived luminance (Rec. 709) */
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));

    /* Soft knee: smoothly ramp from 0 to 1 around the threshold */
    float knee = pc.threshold - pc.soft_threshold;
    float soft = clamp((luma - knee) / (2.0 * pc.soft_threshold + 0.0001), 0.0, 1.0);
    soft = soft * soft; /* quadratic falloff for smoother transition */

    float contrib = max(luma - pc.threshold, 0.0) + soft * min(luma, pc.threshold);
    float factor  = contrib / (luma + 0.0001);

    out_color = vec4(color * factor, 1.0);
}
