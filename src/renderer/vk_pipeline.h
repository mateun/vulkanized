#ifndef ENGINE_VK_PIPELINE_H
#define ENGINE_VK_PIPELINE_H

#include "renderer/vk_types.h"
#include "core/common.h"

EngineResult vk_create_render_pass(VulkanContext *ctx);
EngineResult vk_create_graphics_pipeline(VulkanContext *ctx);
EngineResult vk_create_text_pipeline(VulkanContext *ctx);

#endif /* ENGINE_VK_PIPELINE_H */
