#include "renderer/primitives.h"
#include "renderer/renderer.h"
#include "core/log.h"

#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* --------------------------------------------------------------------------
 * Cube: 24 vertices (4 per face), 36 indices
 * Centered at origin, side length 1 (-0.5 to 0.5)
 * ------------------------------------------------------------------------ */

EngineResult renderer_create_cube(Renderer *renderer, MeshHandle *out_handle) {
    /* clang-format off */
    Vertex3D verts[24] = {
        /* +Z face (front) — normal (0,0,1) */
        {{ -0.5f, -0.5f,  0.5f }, {  0, 0, 1 }, { 0, 1 }, { 1, 1, 1 }},
        {{  0.5f, -0.5f,  0.5f }, {  0, 0, 1 }, { 1, 1 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f,  0.5f }, {  0, 0, 1 }, { 1, 0 }, { 1, 1, 1 }},
        {{ -0.5f,  0.5f,  0.5f }, {  0, 0, 1 }, { 0, 0 }, { 1, 1, 1 }},

        /* -Z face (back) — normal (0,0,-1) */
        {{  0.5f, -0.5f, -0.5f }, {  0, 0, -1 }, { 0, 1 }, { 1, 1, 1 }},
        {{ -0.5f, -0.5f, -0.5f }, {  0, 0, -1 }, { 1, 1 }, { 1, 1, 1 }},
        {{ -0.5f,  0.5f, -0.5f }, {  0, 0, -1 }, { 1, 0 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f, -0.5f }, {  0, 0, -1 }, { 0, 0 }, { 1, 1, 1 }},

        /* +X face (right) — normal (1,0,0) */
        {{  0.5f, -0.5f,  0.5f }, {  1, 0, 0 }, { 0, 1 }, { 1, 1, 1 }},
        {{  0.5f, -0.5f, -0.5f }, {  1, 0, 0 }, { 1, 1 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f, -0.5f }, {  1, 0, 0 }, { 1, 0 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f,  0.5f }, {  1, 0, 0 }, { 0, 0 }, { 1, 1, 1 }},

        /* -X face (left) — normal (-1,0,0) */
        {{ -0.5f, -0.5f, -0.5f }, { -1, 0, 0 }, { 0, 1 }, { 1, 1, 1 }},
        {{ -0.5f, -0.5f,  0.5f }, { -1, 0, 0 }, { 1, 1 }, { 1, 1, 1 }},
        {{ -0.5f,  0.5f,  0.5f }, { -1, 0, 0 }, { 1, 0 }, { 1, 1, 1 }},
        {{ -0.5f,  0.5f, -0.5f }, { -1, 0, 0 }, { 0, 0 }, { 1, 1, 1 }},

        /* +Y face (top) — normal (0,1,0) */
        {{ -0.5f,  0.5f,  0.5f }, {  0, 1, 0 }, { 0, 1 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f,  0.5f }, {  0, 1, 0 }, { 1, 1 }, { 1, 1, 1 }},
        {{  0.5f,  0.5f, -0.5f }, {  0, 1, 0 }, { 1, 0 }, { 1, 1, 1 }},
        {{ -0.5f,  0.5f, -0.5f }, {  0, 1, 0 }, { 0, 0 }, { 1, 1, 1 }},

        /* -Y face (bottom) — normal (0,-1,0) */
        {{ -0.5f, -0.5f, -0.5f }, {  0, -1, 0 }, { 0, 1 }, { 1, 1, 1 }},
        {{  0.5f, -0.5f, -0.5f }, {  0, -1, 0 }, { 1, 1 }, { 1, 1, 1 }},
        {{  0.5f, -0.5f,  0.5f }, {  0, -1, 0 }, { 1, 0 }, { 1, 1, 1 }},
        {{ -0.5f, -0.5f,  0.5f }, {  0, -1, 0 }, { 0, 0 }, { 1, 1, 1 }},
    };

    u32 indices[36] = {
         0,  1,  2,   2,  3,  0,   /* +Z */
         4,  5,  6,   6,  7,  4,   /* -Z */
         8,  9, 10,  10, 11,  8,   /* +X */
        12, 13, 14,  14, 15, 12,   /* -X */
        16, 17, 18,  18, 19, 16,   /* +Y */
        20, 21, 22,  22, 23, 20,   /* -Y */
    };
    /* clang-format on */

    return renderer_upload_mesh_3d(renderer, verts, 24, indices, 36, out_handle);
}

/* --------------------------------------------------------------------------
 * Sphere: UV sphere with configurable segments and rings
 * Centered at origin, radius 0.5 (diameter 1)
 * ------------------------------------------------------------------------ */

EngineResult renderer_create_sphere(Renderer *renderer, u32 segments, u32 rings,
                                    MeshHandle *out_handle) {
    if (segments < 3) segments = 3;
    if (rings < 2) rings = 2;

    u32 vert_count = (segments + 1) * (rings + 1);
    u32 idx_count  = segments * rings * 6;

    Vertex3D *verts = malloc(sizeof(Vertex3D) * vert_count);
    u32      *indices = malloc(sizeof(u32) * idx_count);
    if (!verts || !indices) {
        free(verts);
        free(indices);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    /* Generate vertices */
    u32 vi = 0;
    for (u32 r = 0; r <= rings; r++) {
        f32 phi = (f32)M_PI * (f32)r / (f32)rings; /* 0 to PI */
        f32 sp = sinf(phi);
        f32 cp = cosf(phi);

        for (u32 s = 0; s <= segments; s++) {
            f32 theta = 2.0f * (f32)M_PI * (f32)s / (f32)segments; /* 0 to 2*PI */
            f32 st = sinf(theta);
            f32 ct = cosf(theta);

            f32 nx = sp * ct;
            f32 ny = cp;
            f32 nz = sp * st;

            verts[vi].position[0] = nx * 0.5f;
            verts[vi].position[1] = ny * 0.5f;
            verts[vi].position[2] = nz * 0.5f;
            verts[vi].normal[0]   = nx;
            verts[vi].normal[1]   = ny;
            verts[vi].normal[2]   = nz;
            verts[vi].uv[0]       = (f32)s / (f32)segments;
            verts[vi].uv[1]       = (f32)r / (f32)rings;
            verts[vi].color[0]    = 1.0f;
            verts[vi].color[1]    = 1.0f;
            verts[vi].color[2]    = 1.0f;
            vi++;
        }
    }

    /* Generate indices */
    u32 ii = 0;
    for (u32 r = 0; r < rings; r++) {
        for (u32 s = 0; s < segments; s++) {
            u32 a = r * (segments + 1) + s;
            u32 b = a + (segments + 1);

            /* Two triangles per quad (CCW winding) */
            indices[ii++] = a;
            indices[ii++] = b;
            indices[ii++] = a + 1;

            indices[ii++] = a + 1;
            indices[ii++] = b;
            indices[ii++] = b + 1;
        }
    }

    EngineResult res = renderer_upload_mesh_3d(renderer, verts, vert_count,
                                               indices, idx_count, out_handle);
    free(verts);
    free(indices);
    return res;
}

/* --------------------------------------------------------------------------
 * Cylinder: barrel + top/bottom caps
 * Centered at origin, radius 0.5, height 1 (-0.5 to 0.5 on Y)
 * ------------------------------------------------------------------------ */

EngineResult renderer_create_cylinder(Renderer *renderer, u32 segments,
                                      MeshHandle *out_handle) {
    if (segments < 3) segments = 3;

    /* Barrel: (segments+1)*2 verts, segments*6 indices
     * Top cap: segments+1 verts (center + rim), segments*3 indices
     * Bottom cap: segments+1 verts (center + rim), segments*3 indices */
    u32 barrel_verts = (segments + 1) * 2;
    u32 cap_verts    = segments + 1; /* center + rim points */
    u32 vert_count   = barrel_verts + cap_verts * 2;

    u32 barrel_idx = segments * 6;
    u32 cap_idx    = segments * 3;
    u32 idx_count  = barrel_idx + cap_idx * 2;

    Vertex3D *verts = malloc(sizeof(Vertex3D) * vert_count);
    u32      *indices = malloc(sizeof(u32) * idx_count);
    if (!verts || !indices) {
        free(verts);
        free(indices);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    u32 vi = 0;
    u32 ii = 0;

    /* Barrel */
    for (u32 s = 0; s <= segments; s++) {
        f32 theta = 2.0f * (f32)M_PI * (f32)s / (f32)segments;
        f32 ct = cosf(theta);
        f32 st = sinf(theta);
        f32 u  = (f32)s / (f32)segments;

        /* Bottom ring */
        verts[vi].position[0] = ct * 0.5f;
        verts[vi].position[1] = -0.5f;
        verts[vi].position[2] = st * 0.5f;
        verts[vi].normal[0]   = ct;
        verts[vi].normal[1]   = 0.0f;
        verts[vi].normal[2]   = st;
        verts[vi].uv[0]       = u;
        verts[vi].uv[1]       = 1.0f;
        verts[vi].color[0]    = 1.0f;
        verts[vi].color[1]    = 1.0f;
        verts[vi].color[2]    = 1.0f;
        vi++;

        /* Top ring */
        verts[vi].position[0] = ct * 0.5f;
        verts[vi].position[1] = 0.5f;
        verts[vi].position[2] = st * 0.5f;
        verts[vi].normal[0]   = ct;
        verts[vi].normal[1]   = 0.0f;
        verts[vi].normal[2]   = st;
        verts[vi].uv[0]       = u;
        verts[vi].uv[1]       = 0.0f;
        verts[vi].color[0]    = 1.0f;
        verts[vi].color[1]    = 1.0f;
        verts[vi].color[2]    = 1.0f;
        vi++;
    }

    /* Barrel indices */
    for (u32 s = 0; s < segments; s++) {
        u32 bl = s * 2;
        u32 tl = bl + 1;
        u32 br = bl + 2;
        u32 tr = bl + 3;

        indices[ii++] = bl;
        indices[ii++] = br;
        indices[ii++] = tl;

        indices[ii++] = tl;
        indices[ii++] = br;
        indices[ii++] = tr;
    }

    /* Top cap */
    u32 top_center = vi;
    verts[vi].position[0] = 0.0f;
    verts[vi].position[1] = 0.5f;
    verts[vi].position[2] = 0.0f;
    verts[vi].normal[0]   = 0.0f;
    verts[vi].normal[1]   = 1.0f;
    verts[vi].normal[2]   = 0.0f;
    verts[vi].uv[0]       = 0.5f;
    verts[vi].uv[1]       = 0.5f;
    verts[vi].color[0]    = 1.0f;
    verts[vi].color[1]    = 1.0f;
    verts[vi].color[2]    = 1.0f;
    vi++;

    u32 top_rim_start = vi;
    for (u32 s = 0; s < segments; s++) {
        f32 theta = 2.0f * (f32)M_PI * (f32)s / (f32)segments;
        f32 ct = cosf(theta);
        f32 st = sinf(theta);

        verts[vi].position[0] = ct * 0.5f;
        verts[vi].position[1] = 0.5f;
        verts[vi].position[2] = st * 0.5f;
        verts[vi].normal[0]   = 0.0f;
        verts[vi].normal[1]   = 1.0f;
        verts[vi].normal[2]   = 0.0f;
        verts[vi].uv[0]       = ct * 0.5f + 0.5f;
        verts[vi].uv[1]       = st * 0.5f + 0.5f;
        verts[vi].color[0]    = 1.0f;
        verts[vi].color[1]    = 1.0f;
        verts[vi].color[2]    = 1.0f;
        vi++;
    }

    for (u32 s = 0; s < segments; s++) {
        u32 next = (s + 1) % segments;
        indices[ii++] = top_center;
        indices[ii++] = top_rim_start + s;
        indices[ii++] = top_rim_start + next;
    }

    /* Bottom cap */
    u32 bot_center = vi;
    verts[vi].position[0] = 0.0f;
    verts[vi].position[1] = -0.5f;
    verts[vi].position[2] = 0.0f;
    verts[vi].normal[0]   = 0.0f;
    verts[vi].normal[1]   = -1.0f;
    verts[vi].normal[2]   = 0.0f;
    verts[vi].uv[0]       = 0.5f;
    verts[vi].uv[1]       = 0.5f;
    verts[vi].color[0]    = 1.0f;
    verts[vi].color[1]    = 1.0f;
    verts[vi].color[2]    = 1.0f;
    vi++;

    u32 bot_rim_start = vi;
    for (u32 s = 0; s < segments; s++) {
        f32 theta = 2.0f * (f32)M_PI * (f32)s / (f32)segments;
        f32 ct = cosf(theta);
        f32 st = sinf(theta);

        verts[vi].position[0] = ct * 0.5f;
        verts[vi].position[1] = -0.5f;
        verts[vi].position[2] = st * 0.5f;
        verts[vi].normal[0]   = 0.0f;
        verts[vi].normal[1]   = -1.0f;
        verts[vi].normal[2]   = 0.0f;
        verts[vi].uv[0]       = ct * 0.5f + 0.5f;
        verts[vi].uv[1]       = st * 0.5f + 0.5f;
        verts[vi].color[0]    = 1.0f;
        verts[vi].color[1]    = 1.0f;
        verts[vi].color[2]    = 1.0f;
        vi++;
    }

    for (u32 s = 0; s < segments; s++) {
        u32 next = (s + 1) % segments;
        /* Reversed winding for bottom cap (face downward) */
        indices[ii++] = bot_center;
        indices[ii++] = bot_rim_start + next;
        indices[ii++] = bot_rim_start + s;
    }

    EngineResult res = renderer_upload_mesh_3d(renderer, verts, vi,
                                               indices, ii, out_handle);
    free(verts);
    free(indices);
    return res;
}
