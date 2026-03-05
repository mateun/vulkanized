#version 450

/* Per-vertex (binding 0) — SkinnedVertex3D */
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_color;
layout(location = 4) in uvec4 in_joints;
layout(location = 5) in vec4 in_weights;

/* Per-instance (binding 1) — InstanceData3D (reused from non-skinned) */
layout(location = 6) in vec3 inst_position;
layout(location = 7) in vec3 inst_rotation;  /* pitch, yaw, roll (radians) */
layout(location = 8) in vec3 inst_scale;
layout(location = 9) in vec3 inst_color;

/* Push constants */
layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
    uint joint_offset;   /* byte offset into SSBO for this draw's joint matrices */
    uint joint_count;    /* number of joints */
} pc;

/* Joint matrices SSBO (set 2, binding 0) */
layout(std430, set = 2, binding = 0) readonly buffer JointBuffer {
    mat4 joint_matrices[];
} joints;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal_world;
layout(location = 3) out vec3 frag_pos_world;

void main() {
    /* ---- Skeletal skinning ---- */
    /* joint_offset is in bytes; divide by sizeof(mat4)=64 to get matrix index */
    uint base = pc.joint_offset / 64u;

    mat4 skin_mat =
        in_weights.x * joints.joint_matrices[base + in_joints.x] +
        in_weights.y * joints.joint_matrices[base + in_joints.y] +
        in_weights.z * joints.joint_matrices[base + in_joints.z] +
        in_weights.w * joints.joint_matrices[base + in_joints.w];

    vec4 skinned_pos    = skin_mat * vec4(in_position, 1.0);
    vec3 skinned_normal = mat3(skin_mat) * in_normal;

    /* ---- Instance transform (same as mesh3d.vert) ---- */
    float cp = cos(inst_rotation.x); float sp = sin(inst_rotation.x); /* pitch (X) */
    float cy = cos(inst_rotation.y); float sy = sin(inst_rotation.y); /* yaw   (Y) */
    float cr = cos(inst_rotation.z); float sr = sin(inst_rotation.z); /* roll  (Z) */

    mat3 rot = mat3(
        vec3( cy*cr + sy*sp*sr,   cp*sr,  -sy*cr + cy*sp*sr),
        vec3(-cy*sr + sy*sp*cr,   cp*cr,   sy*sr + cy*sp*cr),
        vec3( sy*cp,             -sp,      cy*cp            )
    );

    vec3 scaled   = skinned_pos.xyz * inst_scale;
    vec3 rotated  = rot * scaled;
    vec3 world_pos = rotated + inst_position;

    gl_Position = pc.vp * vec4(world_pos, 1.0);

    /* Transform normal (rotation only — correct for uniform scale) */
    frag_normal_world = normalize(rot * normalize(skinned_normal));
    frag_pos_world    = world_pos;

    /* Color tinting */
    frag_color = in_color * inst_color;
    frag_uv    = in_uv;
}
