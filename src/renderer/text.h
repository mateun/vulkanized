#ifndef ENGINE_TEXT_H
#define ENGINE_TEXT_H

#include "renderer/vk_types.h"
#include "core/common.h"

/* Maximum characters we can render in a single frame */
#define TEXT_MAX_CHARS 4096

/* Initialize text rendering: load font, bake atlas, create GPU resources. */
EngineResult text_init(VulkanContext *ctx, const char *font_path, f32 font_size);

/* Queue text for rendering. Call multiple times per frame before text_flush().
 * scale: 1.0 = baked font size, 0.5 = half size, etc.  */
void text_draw(VulkanContext *ctx, const char *str,
               f32 x, f32 y, f32 scale, f32 r, f32 g, f32 b);

/* Upload queued text vertices and record draw commands into the given command buffer.
 * Must be called inside a render pass. */
void text_flush(VulkanContext *ctx, VkCommandBuffer cmd);

/* Destroy text rendering resources. */
void text_shutdown(VulkanContext *ctx);

#endif /* ENGINE_TEXT_H */
