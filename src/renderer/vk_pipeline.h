#ifndef ENGINE_VK_PIPELINE_H
#define ENGINE_VK_PIPELINE_H

#include "renderer/vk_types.h"
#include "core/common.h"

#include <stddef.h>

/* Shared shader helpers (used by bloom.c too) */
u8            *vk_read_file(const char *path, size_t *out_size);
VkShaderModule vk_create_shader_module(VkDevice device, const u8 *code, size_t size);

EngineResult vk_create_render_pass(VulkanContext *ctx);
EngineResult vk_create_graphics_pipeline(VulkanContext *ctx);
EngineResult vk_create_text_pipeline(VulkanContext *ctx);

/* Create geometry + text pipelines against the bloom HDR render pass */
EngineResult vk_create_bloom_scene_pipelines(VulkanContext *ctx);

/* 3D pipeline (Phong-lit, uses mesh3d.vert/frag) */
EngineResult vk_create_3d_pipeline(VulkanContext *ctx);

/* 3D pipeline against bloom HDR render pass */
EngineResult vk_create_bloom_scene_3d_pipeline(VulkanContext *ctx);

#endif /* ENGINE_VK_PIPELINE_H */
