#ifndef ENGINE_BLOOM_H
#define ENGINE_BLOOM_H

#include "core/common.h"

/* Forward decl — bloom hides Vulkan from the public API */
typedef struct VulkanContext VulkanContext;

/* ---- Bloom settings (no Vulkan dependency, safe for game code) ---- */

typedef struct {
    f32 intensity;          /* 0.0 = off, 0.8 = default */
    f32 threshold;          /* luminance cutoff, 0.6 default */
    f32 soft_threshold;     /* soft knee width, 0.3 default */
    f32 scanline_strength;  /* 0.0 = off, 0.15 = subtle */
    f32 scanline_count;     /* lines across height, 360 default */
    f32 aberration;         /* chromatic offset in pixels, 1.5 default */
} BloomSettings;

/* Sensible defaults */
#define BLOOM_SETTINGS_DEFAULT { \
    .intensity       = 0.8f,     \
    .threshold       = 0.6f,     \
    .soft_threshold  = 0.3f,     \
    .scanline_strength = 0.15f,  \
    .scanline_count  = 360.0f,   \
    .aberration      = 1.5f,     \
}

/* ---- Lifecycle (called from renderer.c) ---- */

EngineResult bloom_init(VulkanContext *vk);
void         bloom_shutdown(VulkanContext *vk);

/* Resize — destroy and recreate all size-dependent resources.
 * Called after swapchain recreation. */
EngineResult bloom_resize(VulkanContext *vk);

/* Destroy only the resources that depend on swapchain image views.
 * Must be called BEFORE vk_cleanup_swapchain(). */
void         bloom_cleanup_swapchain_deps(VulkanContext *vk);

/* Record bloom passes 2-5 (extract, blur H, blur V, composite).
 * Pass 1 (scene to HDR) is handled by the caller in record_command_buffer().
 * NOTE: This declaration requires Vulkan headers — only available when
 * vk_types.h has been included before bloom.h (i.e. internal engine code). */
#ifdef VK_VERSION_1_0
void         bloom_record(VulkanContext *vk, VkCommandBuffer cmd,
                          const BloomSettings *settings, u32 image_index);
#endif

#endif /* ENGINE_BLOOM_H */
