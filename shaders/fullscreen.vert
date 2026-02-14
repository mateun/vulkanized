#version 450

/* Fullscreen triangle — no vertex buffer needed.
 * Generates a triangle that covers the entire screen using gl_VertexIndex.
 * Vertices: (-1,-1), (3,-1), (-1,3)  =>  covers the [0,1] UV quad. */

layout(location = 0) out vec2 frag_uv;

void main() {
    vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    frag_uv     = positions[gl_VertexIndex] * 0.5 + 0.5;
}
