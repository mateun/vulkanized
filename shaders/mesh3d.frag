#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal_world;
layout(location = 3) in vec3 frag_pos_world;
layout(location = 4) in vec4 frag_pos_light_space;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
} pc;

/* Directional light + shadow (set 1, binding 0) */
layout(set = 1, binding = 0) uniform LightUBO {
    vec4 light_dir;    /* xyz = direction (normalized, FROM light), w = unused */
    vec4 light_color;  /* xyz = color, w = unused */
    vec4 ambient;      /* xyz = ambient color, w = unused */
    vec4 view_pos;     /* xyz = camera position, w = unused */
    vec4 shininess;    /* x = specular exponent, yzw = unused */
    mat4 light_vp;     /* light view-projection (unused in frag, but part of UBO) */
} light;

/* Shadow map (set 2, binding 0) — comparison sampler for hardware PCF */
layout(set = 2, binding = 0) uniform sampler2DShadow shadow_map;

const float SHADOW_BIAS = 0.005;

float calculate_shadow() {
    /* Perspective divide (clip -> NDC) */
    vec3 proj = frag_pos_light_space.xyz / frag_pos_light_space.w;

    /* XY: NDC [-1,1] -> texture coords [0,1] */
    vec2 shadow_uv = proj.xy * 0.5 + 0.5;

    /* Z is already in [0,1] (Vulkan-native ortho projection) */
    float current_depth = proj.z;

    /* Outside shadow map frustum = fully lit */
    if (shadow_uv.x < 0.0 || shadow_uv.x > 1.0 ||
        shadow_uv.y < 0.0 || shadow_uv.y > 1.0 ||
        current_depth < 0.0 || current_depth > 1.0) {
        return 1.0;
    }

    /* 3x3 PCF (percentage closer filtering) for soft shadow edges */
    float shadow = 0.0;
    vec2 texel_size = 1.0 / textureSize(shadow_map, 0);
    for (int x = -1; x <= 1; x++) {
        for (int y = -1; y <= 1; y++) {
            vec2 offset = vec2(float(x), float(y)) * texel_size;
            shadow += texture(shadow_map, vec3(shadow_uv + offset, current_depth - SHADOW_BIAS));
        }
    }
    shadow /= 9.0;

    return shadow;
}

void main() {
    vec3 base_color = frag_color;
    float alpha = 1.0;

    if (pc.use_texture != 0u) {
        vec4 tex_sample = texture(tex, frag_uv);
        base_color *= tex_sample.rgb;
        alpha = tex_sample.a;
    }

    if (alpha < 0.01)
        discard;

    vec3 N = normalize(frag_normal_world);
    vec3 L = normalize(-light.light_dir.xyz); /* toward light */

    /* Flip normal if it faces away from the viewer (two-sided lighting) */
    vec3 V = normalize(light.view_pos.xyz - frag_pos_world);
    if (dot(N, V) < 0.0)
        N = -N;

    /* Shadow factor (1.0 = fully lit, 0.0 = fully shadowed) */
    float shadow = calculate_shadow();

    /* Ambient */
    vec3 ambient = light.ambient.xyz * base_color;

    /* Diffuse */
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * light.light_color.xyz * base_color;

    /* Specular (Phong) */
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), light.shininess.x);
    vec3 specular = spec * light.light_color.xyz;

    /* Shadow affects diffuse + specular, not ambient */
    vec3 result = ambient + shadow * (diffuse + specular);
    out_color = vec4(result, alpha);
}
