#version 450

/* Per-vertex (binding 0) — same layout as mesh3d.vert */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;   /* unused but must match binding */
layout(location = 2) in vec2 in_uv;       /* unused */
layout(location = 3) in vec3 in_color;    /* unused */

/* Per-instance (binding 1) — same layout as mesh3d.vert */
layout(location = 4) in vec3 inst_position;
layout(location = 5) in vec3 inst_rotation;
layout(location = 6) in vec3 inst_scale;
layout(location = 7) in vec3 inst_color;  /* unused */

/* Light view-projection matrix */
layout(push_constant) uniform PushConstants {
    mat4 light_vp;
} pc;

void main() {
    /* Build rotation matrix from Euler angles: R = Ry * Rx * Rz */
    float cp = cos(inst_rotation.x); float sp = sin(inst_rotation.x);
    float cy = cos(inst_rotation.y); float sy = sin(inst_rotation.y);
    float cr = cos(inst_rotation.z); float sr = sin(inst_rotation.z);

    mat3 rot = mat3(
        vec3( cy*cr + sy*sp*sr,   cp*sr,  -sy*cr + cy*sp*sr),
        vec3(-cy*sr + sy*sp*cr,   cp*cr,   sy*sr + cy*sp*cr),
        vec3( sy*cp,             -sp,      cy*cp            )
    );

    vec3 scaled   = in_position * inst_scale;
    vec3 rotated  = rot * scaled;
    vec3 world_pos = rotated + inst_position;

    gl_Position = pc.light_vp * vec4(world_pos, 1.0);
}
