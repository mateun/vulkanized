#version 450

/* Per-vertex (binding 0) */
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_color;

/* Per-instance (binding 1) */
layout(location = 3) in vec2 inst_position;
layout(location = 4) in float inst_rotation;
layout(location = 5) in vec2 inst_scale;
layout(location = 6) in vec3 inst_color;
layout(location = 7) in vec2 inst_uv_offset;
layout(location = 8) in vec2 inst_uv_scale;

/* View-projection matrix + texture flag from camera */
layout(push_constant) uniform PushConstants {
    mat4 vp;
    uint use_texture;
} pc;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;

void main() {
    /* Apply instance scale */
    vec2 scaled = in_position * inst_scale;

    /* Apply instance rotation (2D) */
    float c = cos(inst_rotation);
    float s = sin(inst_rotation);
    vec2 rotated = vec2(
        scaled.x * c - scaled.y * s,
        scaled.x * s + scaled.y * c
    );

    /* Apply instance position (world space) */
    vec2 world_pos = rotated + inst_position;

    /* Transform through camera VP */
    gl_Position = pc.vp * vec4(world_pos, 0.0, 1.0);

    /* Vertex color × instance color tint */
    frag_color = in_color * inst_color;

    /* Sprite sheet UV remap: offset + scale per-instance.
     * If uv_scale == (0,0) (zero-initialized default), pass UV through unchanged. */
    if (inst_uv_scale.x > 0.0 || inst_uv_scale.y > 0.0)
        frag_uv = in_uv * inst_uv_scale + inst_uv_offset;
    else
        frag_uv = in_uv;
}
