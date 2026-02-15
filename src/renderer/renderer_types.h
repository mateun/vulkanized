#ifndef ENGINE_RENDERER_TYPES_H
#define ENGINE_RENDERER_TYPES_H

#include "core/common.h"

/* ---- Vertex formats (public — no Vulkan dependency) ---- */

typedef struct {
    f32 position[2]; /* x, y */
    f32 uv[2];       /* u, v  (0,0 for untextured meshes) */
    f32 color[3];    /* r, g, b */
} Vertex;

typedef struct {
    f32 position[2]; /* x, y  (screen pixels, transformed by ortho) */
    f32 uv[2];       /* u, v  (into font atlas) */
    f32 color[3];    /* r, g, b */
} TextVertex;

/* ---- Per-instance data for instanced rendering ---- */

typedef struct {
    f32 position[2];  /* x, y world position */
    f32 rotation;     /* radians */
    f32 scale[2];     /* x, y scale */
    f32 color[3];     /* r, g, b — multiplied with vertex color */
    f32 uv_offset[2]; /* sprite sheet: top-left UV of the tile (default 0,0) */
    f32 uv_scale[2];  /* sprite sheet: tile size in UV space (0,0 = full texture) */
} InstanceData;

/* ---- 2D Camera ---- */

typedef struct {
    f32 position[2]; /* world-space center of view */
    f32 rotation;    /* radians, counter-clockwise */
    f32 zoom;        /* 1.0 = default, >1 = zoom in, <1 = zoom out */
    f32 half_height; /* half the visible world height (0 = default 10.0) */
} Camera2D;

/* ---- Mesh handle (opaque to the game, returned by renderer_upload_mesh) ---- */

typedef u32 MeshHandle;
#define MESH_HANDLE_INVALID ((MeshHandle)0xFFFFFFFF)

/* ---- Texture filter mode (passed to renderer_load_texture) ---- */

typedef enum {
    TEXTURE_FILTER_SMOOTH,    /* bilinear filtering — good for photos, gradients */
    TEXTURE_FILTER_PIXELART,  /* nearest-neighbor — sharp pixels, no blurring */
} TextureFilter;

/* ---- Texture handle (opaque to the game, returned by renderer_load_texture) ---- */

typedef u32 TextureHandle;
#define TEXTURE_HANDLE_INVALID ((TextureHandle)0xFFFFFFFF)

#endif /* ENGINE_RENDERER_TYPES_H */
