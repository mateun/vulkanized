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

/* ---- 3D Vertex format ---- */

typedef struct {
    f32 position[3]; /* x, y, z */
    f32 normal[3];   /* nx, ny, nz */
    f32 uv[2];       /* u, v */
    f32 color[3];    /* r, g, b */
} Vertex3D;          /* 44 bytes */

/* ---- 3D Per-instance data ---- */

typedef struct {
    f32 position[3]; /* x, y, z world position */
    f32 rotation[3]; /* pitch, yaw, roll (Euler angles, radians) */
    f32 scale[3];    /* x, y, z scale */
    f32 color[3];    /* r, g, b — multiplied with vertex color */
} InstanceData3D;    /* 48 bytes */

/* ---- 3D Camera ---- */

typedef struct {
    f32 position[3]; /* eye position in world space */
    f32 target[3];   /* look-at target */
    f32 up[3];       /* up vector (typically 0,1,0) */
    f32 fov;         /* vertical field of view in degrees */
    f32 near_plane;  /* near clipping plane */
    f32 far_plane;   /* far clipping plane */
} Camera3D;

/* ---- Directional light ---- */

typedef struct {
    f32 direction[3]; /* normalized, pointing FROM light toward scene */
    f32 color[3];     /* light color (r, g, b) */
    f32 ambient[3];   /* ambient light color (r, g, b) */
    f32 shininess;    /* specular exponent (e.g. 32.0) */
} DirectionalLight;

/* ---- Texture handle (opaque to the game, returned by renderer_load_texture) ---- */

typedef u32 TextureHandle;
#define TEXTURE_HANDLE_INVALID ((TextureHandle)0xFFFFFFFF)

#endif /* ENGINE_RENDERER_TYPES_H */
