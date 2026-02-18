#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal_world;
layout(location = 3) in vec3 frag_pos_world;

layout(location = 0) out vec4 out_color;

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
} pc;

/* Directional light (set 1, binding 0) */
layout(set = 1, binding = 0) uniform LightUBO {
    vec4 light_dir;    /* xyz = direction (normalized, FROM light), w = unused */
    vec4 light_color;  /* xyz = color, w = unused */
    vec4 ambient;      /* xyz = ambient color, w = unused */
    vec4 view_pos;     /* xyz = camera position, w = unused */
    vec4 shininess;    /* x = specular exponent, yzw = unused */
} light;

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

    /* Ambient */
    vec3 ambient = light.ambient.xyz * base_color;

    /* Diffuse */
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * light.light_color.xyz * base_color;

    /* Specular (Phong) */
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(V, R), 0.0), light.shininess.x);
    vec3 specular = spec * light.light_color.xyz;

    vec3 result = ambient + diffuse + specular;
    out_color = vec4(result, alpha);
}
