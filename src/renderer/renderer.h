#ifndef ENGINE_RENDERER_H
#define ENGINE_RENDERER_H

#include "core/common.h"
#include "renderer/renderer_types.h"
#include "renderer/bloom.h"

/* Forward decls — renderer hides Vulkan from the rest of the engine */
typedef struct Window Window;
typedef struct Renderer Renderer;

typedef struct {
    const char *font_path;   /* e.g. "assets/consolas.ttf" */
    f32         font_size;   /* e.g. 24.0f */
    f32         clear_color[4]; /* r, g, b, a — background clear color (default: dark grey) */
} RendererConfig;

/* Lifecycle */
EngineResult renderer_create(Window *window, const RendererConfig *config,
                             Renderer **out_renderer);
void         renderer_destroy(Renderer *renderer);

/* Frame control — game calls begin, issues draw calls, then calls end */
EngineResult renderer_begin_frame(Renderer *renderer);
EngineResult renderer_end_frame(Renderer *renderer);

/* Change the background clear color at runtime */
void         renderer_set_clear_color(Renderer *renderer, f32 r, f32 g, f32 b, f32 a);

/* Camera — sets the active view-projection for subsequent draw_mesh calls.
 * Can be called multiple times per frame (e.g. main view + minimap).
 * If never called, a default identity camera at (0,0) zoom=1 is used. */
void         renderer_set_camera(Renderer *renderer, const Camera2D *camera);

/* Geometry upload — returns a MeshHandle for use with draw_mesh.
 * Multiple meshes can be uploaded; they share one GPU vertex buffer. */
EngineResult renderer_upload_mesh(Renderer *renderer,
                                  const Vertex *vertices, u32 count,
                                  MeshHandle *out_handle);

/* Texture loading — decodes an image file (PNG/JPG/BMP) and uploads to GPU.
 * filter: TEXTURE_FILTER_PIXELART for sharp pixels, TEXTURE_FILTER_SMOOTH for bilinear.
 * Returns a TextureHandle for use with renderer_draw_mesh_textured(). */
EngineResult renderer_load_texture(Renderer *renderer, const char *path,
                                   TextureFilter filter,
                                   TextureHandle *out_handle);

/* Instanced mesh draw — call between begin_frame and end_frame.
 * Draws the given mesh once per instance with the given transforms. */
void         renderer_draw_mesh(Renderer *renderer, MeshHandle mesh,
                                const InstanceData *instances, u32 instance_count);

/* Textured instanced mesh draw — same as draw_mesh but samples the given texture.
 * Mesh vertices should have UV coordinates set. */
void         renderer_draw_mesh_textured(Renderer *renderer, MeshHandle mesh,
                                         TextureHandle texture,
                                         const InstanceData *instances,
                                         u32 instance_count);

/* Text drawing — call between begin_frame and end_frame */
void         renderer_draw_text(Renderer *renderer, const char *str,
                                f32 x, f32 y, f32 scale,
                                f32 r, f32 g, f32 b);

/* Query current swapchain extent in pixels */
void         renderer_get_extent(const Renderer *renderer,
                                 u32 *width, u32 *height);

/* Called when window is resized to recreate swapchain */
EngineResult renderer_handle_resize(Renderer *renderer);

/* Bloom post-processing — enable/disable and configure.
 * When enabled, the renderer uses an HDR offscreen target with bloom, scanlines,
 * chromatic aberration, and vignette for an 80s arcade neon look. */
void         renderer_set_bloom(Renderer *renderer, bool enabled,
                                f32 intensity, f32 threshold);
void         renderer_set_bloom_settings(Renderer *renderer, const BloomSettings *settings);

#endif /* ENGINE_RENDERER_H */
