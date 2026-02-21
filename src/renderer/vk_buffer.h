#ifndef ENGINE_VK_BUFFER_H
#define ENGINE_VK_BUFFER_H

#include "renderer/vk_types.h"
#include "core/common.h"

/* Create a Vulkan buffer + device memory allocation. */
EngineResult vk_create_buffer(VulkanContext *ctx,
                              VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags mem_props,
                              VkBuffer *out_buffer,
                              VkDeviceMemory *out_memory);

/* Pre-allocate a shared vertex buffer on the GPU. Call once at init. */
EngineResult vk_create_vertex_buffer(VulkanContext *ctx, u32 max_vertices);

/* Upload a mesh into the shared vertex buffer. Returns the mesh index. */
EngineResult vk_upload_mesh(VulkanContext *ctx,
                            const Vertex *vertices, u32 vertex_count,
                            MeshHandle *out_handle);

/* Create a texture from raw pixel data (R8 single-channel or RGBA).
 * filter: VK_FILTER_NEAREST for pixel art, VK_FILTER_LINEAR for smooth. */
EngineResult vk_create_texture(VulkanContext *ctx,
                               const u8 *pixels,
                               u32 width, u32 height,
                               VkFormat format,
                               VkFilter filter,
                               VulkanTexture *out_tex);

/* Destroy a texture and free its resources. */
void vk_destroy_texture(VulkanContext *ctx, VulkanTexture *tex);

/* 3D vertex buffer (GPU-local, separate from 2D). */
EngineResult vk_create_vertex_buffer_3d(VulkanContext *ctx, u32 max_vertices);

/* Shared index buffer (GPU-local). */
EngineResult vk_create_index_buffer(VulkanContext *ctx, u32 max_indices);

/* Upload a 3D mesh (vertices + optional indices). */
EngineResult vk_upload_mesh_3d(VulkanContext *ctx,
                               const Vertex3D *vertices, u32 vertex_count,
                               const u32 *indices, u32 index_count,
                               MeshHandle *out_handle);

/* Skinned vertex buffer (GPU-local, separate from regular 3D). */
EngineResult vk_create_vertex_buffer_skinned(VulkanContext *ctx, u32 max_vertices);

/* Upload a skinned 3D mesh (SkinnedVertex3D + indices). */
EngineResult vk_upload_mesh_skinned(VulkanContext *ctx,
                                     const SkinnedVertex3D *vertices, u32 vertex_count,
                                     const u32 *indices, u32 index_count,
                                     MeshHandle *out_handle);

/* One-shot command helpers (exposed for text module). */
VkCommandBuffer vk_begin_single_command(VulkanContext *ctx);
void            vk_end_single_command(VulkanContext *ctx, VkCommandBuffer cmd);

#endif /* ENGINE_VK_BUFFER_H */
