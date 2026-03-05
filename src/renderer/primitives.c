#include "renderer/primitives.h"
#include "renderer/renderer.h"
#include "core/log.h"
#include "core/noise.h"

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

/* --------------------------------------------------------------------------
 * Ground plane: vertex grid on XZ plane at Y=0
 * Centered at origin, configurable subdivisions and world-space size.
 * All heights are Y=0 (flat); designed for later noise modulation.
 * ------------------------------------------------------------------------ */

EngineResult renderer_create_ground(Renderer *renderer,
                                    u32 subdivs_x, u32 subdivs_z,
                                    f32 size_x, f32 size_z,
                                    MeshHandle *out_handle) {
    if (subdivs_x < 1) subdivs_x = 1;
    if (subdivs_z < 1) subdivs_z = 1;

    u32 cols = subdivs_x + 1;
    u32 rows = subdivs_z + 1;
    u32 vert_count = cols * rows;
    u32 idx_count  = subdivs_x * subdivs_z * 6;

    Vertex3D *verts  = malloc(sizeof(Vertex3D) * vert_count);
    u32      *indices = malloc(sizeof(u32) * idx_count);
    if (!verts || !indices) {
        free(verts);
        free(indices);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    /* Noise terrain parameters */
    const f32 noise_freq   = 0.3f;   /* spatial frequency */
    const f32 noise_amp    = 0.6f;   /* max height displacement */
    const i32 noise_oct    = 4;
    const f32 noise_lac    = 2.0f;
    const f32 noise_gain   = 0.5f;

    /* Helper: sample terrain height at world (x, z) */
    #define TERRAIN_HEIGHT(wx, wz) \
        (noise_fbm2d((wx) * noise_freq, (wz) * noise_freq, \
                      noise_oct, noise_lac, noise_gain) * noise_amp)

    /* Generate vertices with fBm height displacement */
    f32 half_x = size_x * 0.5f;
    f32 half_z = size_z * 0.5f;
    u32 vi = 0;

    f32 eps = 0.01f;  /* finite-difference epsilon for normal estimation */

    for (u32 rz = 0; rz < rows; rz++) {
        f32 tz = (f32)rz / (f32)subdivs_z;
        f32 z  = -half_z + tz * size_z;

        for (u32 cx = 0; cx < cols; cx++) {
            f32 tx = (f32)cx / (f32)subdivs_x;
            f32 x  = -half_x + tx * size_x;

            f32 y = TERRAIN_HEIGHT(x, z);

            verts[vi].position[0] = x;
            verts[vi].position[1] = y;
            verts[vi].position[2] = z;

            /* Compute normal via finite differences */
            f32 hL = TERRAIN_HEIGHT(x - eps, z);
            f32 hR = TERRAIN_HEIGHT(x + eps, z);
            f32 hD = TERRAIN_HEIGHT(x, z - eps);
            f32 hU = TERRAIN_HEIGHT(x, z + eps);
            f32 nx = hL - hR;
            f32 nz = hD - hU;
            f32 ny = 2.0f * eps;
            f32 len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 0.0f) { nx /= len; ny /= len; nz /= len; }
            else            { nx = 0.0f; ny = 1.0f; nz = 0.0f; }

            verts[vi].normal[0] = nx;
            verts[vi].normal[1] = ny;
            verts[vi].normal[2] = nz;
            verts[vi].uv[0]     = tx;
            verts[vi].uv[1]     = tz;
            verts[vi].color[0]  = 1.0f;
            verts[vi].color[1]  = 1.0f;
            verts[vi].color[2]  = 1.0f;
            vi++;
        }
    }

    #undef TERRAIN_HEIGHT

    /* Generate indices: two triangles per quad, CCW winding (top-facing) */
    u32 ii = 0;
    for (u32 rz = 0; rz < subdivs_z; rz++) {
        for (u32 cx = 0; cx < subdivs_x; cx++) {
            u32 tl = rz * cols + cx;
            u32 tr = tl + 1;
            u32 bl = tl + cols;
            u32 br = bl + 1;

            /* First triangle: tl -> bl -> tr */
            indices[ii++] = tl;
            indices[ii++] = bl;
            indices[ii++] = tr;

            /* Second triangle: tr -> bl -> br */
            indices[ii++] = tr;
            indices[ii++] = bl;
            indices[ii++] = br;
        }
    }

    EngineResult res = renderer_upload_mesh_3d(renderer, verts, vert_count,
                                               indices, idx_count, out_handle);
    free(verts);
    free(indices);
    return res;
}
