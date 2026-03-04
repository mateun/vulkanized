#version 450

/* Per-vertex (binding 0) */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_color;

/* Per-instance (binding 1) */
layout(location = 4) in vec3 inst_position;
layout(location = 5) in vec3 inst_rotation;  /* pitch, yaw, roll (radians) */
layout(location = 6) in vec3 inst_scale;
layout(location = 7) in vec3 inst_color;

/* View-projection matrix + texture flag */
layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
} pc;

/* Directional light + shadow (set 1, binding 0) */
layout(set = 1, binding = 0) uniform LightUBO {
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient;
    vec4 view_pos;
    vec4 shininess;
    mat4 light_vp;
} light;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal_world;
layout(location = 3) out vec3 frag_pos_world;
layout(location = 4) out vec4 frag_pos_light_space;

void main() {
    /* Build rotation matrix from Euler angles: R = Ry * Rx * Rz */
    float cp = cos(inst_rotation.x); float sp = sin(inst_rotation.x); /* pitch (X) */
    float cy = cos(inst_rotation.y); float sy = sin(inst_rotation.y); /* yaw   (Y) */
    float cr = cos(inst_rotation.z); float sr = sin(inst_rotation.z); /* roll  (Z) */

    mat3 rot = mat3(
        vec3( cy*cr + sy*sp*sr,   cp*sr,  -sy*cr + cy*sp*sr),
        vec3(-cy*sr + sy*sp*cr,   cp*cr,   sy*sr + cy*sp*cr),
        vec3( sy*cp,             -sp,      cy*cp            )
    );

    /* Scale, rotate, translate */
    vec3 scaled   = in_position * inst_scale;
    vec3 rotated  = rot * scaled;
    vec3 world_pos = rotated + inst_position;

    gl_Position = pc.vp * vec4(world_pos, 1.0);

    /* Transform normal (rotation only — correct for uniform scale) */
    frag_normal_world = normalize(rot * in_normal);
    frag_pos_world    = world_pos;

    /* Shadow: transform world position into light clip space */
    frag_pos_light_space = light.light_vp * vec4(world_pos, 1.0);

    /* Color tinting */
    frag_color = in_color * inst_color;
    frag_uv    = in_uv;
}
