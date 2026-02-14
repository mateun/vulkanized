#ifndef ENGINE_VK_INIT_H
#define ENGINE_VK_INIT_H

#include "renderer/vk_types.h"
#include "core/common.h"

/* Forward decl */
typedef struct Window Window;

EngineResult vk_create_instance(VulkanContext *ctx);
EngineResult vk_setup_debug_messenger(VulkanContext *ctx);
EngineResult vk_create_surface(VulkanContext *ctx, Window *window);
EngineResult vk_pick_physical_device(VulkanContext *ctx);
EngineResult vk_create_logical_device(VulkanContext *ctx);
EngineResult vk_create_swapchain(VulkanContext *ctx, i32 width, i32 height);
EngineResult vk_create_image_views(VulkanContext *ctx);
EngineResult vk_create_depth_resources(VulkanContext *ctx);
EngineResult vk_create_framebuffers(VulkanContext *ctx);
EngineResult vk_create_command_pool(VulkanContext *ctx);
EngineResult vk_create_command_buffers(VulkanContext *ctx);
EngineResult vk_create_sync_objects(VulkanContext *ctx);

/* Cleanup helpers */
void vk_cleanup_swapchain(VulkanContext *ctx);
void vk_destroy(VulkanContext *ctx);

#endif /* ENGINE_VK_INIT_H */
