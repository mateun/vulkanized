#include "renderer/vk_buffer.h"
#include "core/log.h"

#include <string.h>

/* --------------------------------------------------------------------------
 * Memory type selection
 * ------------------------------------------------------------------------ */

static u32 find_memory_type(VulkanContext *ctx, u32 type_filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(ctx->physical_device, &mem_props);

    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }

    LOG_FATAL("Failed to find suitable memory type");
    return UINT32_MAX;
}

/* --------------------------------------------------------------------------
 * Generic buffer creation
 * ------------------------------------------------------------------------ */

EngineResult vk_create_buffer(VulkanContext *ctx,
                              VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags mem_props,
                              VkBuffer *out_buffer,
                              VkDeviceMemory *out_memory)
{
    VkBufferCreateInfo buf_info = {
        .sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size        = size,
        .usage       = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    if (vkCreateBuffer(ctx->device, &buf_info, NULL, out_buffer) != VK_SUCCESS) {
        LOG_ERROR("Failed to create buffer");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(ctx->device, *out_buffer, &mem_reqs);

    u32 mem_type = find_memory_type(ctx, mem_reqs.memoryTypeBits, mem_props);
    if (mem_type == UINT32_MAX) {
        vkDestroyBuffer(ctx->device, *out_buffer, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, out_memory) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate buffer memory");
        vkDestroyBuffer(ctx->device, *out_buffer, NULL);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    vkBindBufferMemory(ctx->device, *out_buffer, *out_memory, 0);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Single-shot command helpers (for staging transfers)
 * ------------------------------------------------------------------------ */

VkCommandBuffer vk_begin_single_command(VulkanContext *ctx) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(ctx->device, &alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin_info);

    return cmd;
}

void vk_end_single_command(VulkanContext *ctx, VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit = {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmd,
    };

    vkQueueSubmit(ctx->graphics_queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx->graphics_queue);

    vkFreeCommandBuffers(ctx->device, ctx->command_pool, 1, &cmd);
}

/* --------------------------------------------------------------------------
 * Shared vertex buffer (pre-allocated GPU-local, meshes appended via staging)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_vertex_buffer(VulkanContext *ctx, u32 max_vertices) {
    VkDeviceSize buf_size = sizeof(Vertex) * max_vertices;

    EngineResult res = vk_create_buffer(ctx, buf_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &ctx->vertex_buffer, &ctx->vertex_buffer_memory);
    if (res != ENGINE_SUCCESS) return res;

    ctx->vertex_total = 0;
    ctx->mesh_count   = 0;

    LOG_INFO("Shared vertex buffer created: capacity %u vertices (%llu bytes)",
             max_vertices, (unsigned long long)buf_size);
    return ENGINE_SUCCESS;
}

EngineResult vk_upload_mesh(VulkanContext *ctx,
                            const Vertex *vertices, u32 vertex_count,
                            MeshHandle *out_handle)
{
    if (ctx->mesh_count >= MAX_MESHES) {
        LOG_ERROR("Mesh table full (%u/%u)", ctx->mesh_count, MAX_MESHES);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkDeviceSize data_size   = sizeof(Vertex) * vertex_count;
    VkDeviceSize dest_offset = sizeof(Vertex) * ctx->vertex_total;

    /* Staging buffer */
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    EngineResult res = vk_create_buffer(ctx, data_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buf, &staging_mem);
    if (res != ENGINE_SUCCESS) return res;

    void *mapped;
    vkMapMemory(ctx->device, staging_mem, 0, data_size, 0, &mapped);
    memcpy(mapped, vertices, (size_t)data_size);
    vkUnmapMemory(ctx->device, staging_mem);

    /* Copy at offset into the shared vertex buffer */
    VkCommandBuffer cmd = vk_begin_single_command(ctx);
    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = dest_offset,
        .size      = data_size,
    };
    vkCmdCopyBuffer(cmd, staging_buf, ctx->vertex_buffer, 1, &copy_region);
    vk_end_single_command(ctx, cmd);

    vkDestroyBuffer(ctx->device, staging_buf, NULL);
    vkFreeMemory(ctx->device, staging_mem, NULL);

    /* Register mesh slot */
    MeshHandle handle = (MeshHandle)ctx->mesh_count;
    ctx->meshes[handle].first_vertex = ctx->vertex_total;
    ctx->meshes[handle].vertex_count = vertex_count;
    ctx->vertex_total += vertex_count;
    ctx->mesh_count++;

    *out_handle = handle;

    LOG_INFO("Mesh %u uploaded: %u vertices at offset %u",
             handle, vertex_count, ctx->meshes[handle].first_vertex);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * 3D vertex buffer (pre-allocated GPU-local, separate from 2D buffer)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_vertex_buffer_3d(VulkanContext *ctx, u32 max_vertices) {
    VkDeviceSize buf_size = sizeof(Vertex3D) * max_vertices;

    EngineResult res = vk_create_buffer(ctx, buf_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &ctx->vertex_buffer_3d, &ctx->vertex_buffer_3d_memory);
    if (res != ENGINE_SUCCESS) return res;

    ctx->vertex_3d_total = 0;

    LOG_INFO("3D vertex buffer created: capacity %u vertices (%llu bytes)",
             max_vertices, (unsigned long long)buf_size);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Shared index buffer (pre-allocated GPU-local)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_index_buffer(VulkanContext *ctx, u32 max_indices) {
    VkDeviceSize buf_size = sizeof(u32) * max_indices;

    EngineResult res = vk_create_buffer(ctx, buf_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &ctx->index_buffer, &ctx->index_buffer_memory);
    if (res != ENGINE_SUCCESS) return res;

    ctx->index_total = 0;

    LOG_INFO("Index buffer created: capacity %u indices (%llu bytes)",
             max_indices, (unsigned long long)buf_size);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * 3D mesh upload (vertices + optional indices via staging)
 * ------------------------------------------------------------------------ */

EngineResult vk_upload_mesh_3d(VulkanContext *ctx,
                               const Vertex3D *vertices, u32 vertex_count,
                               const u32 *indices, u32 index_count,
                               MeshHandle *out_handle)
{
    if (ctx->mesh_count >= MAX_MESHES) {
        LOG_ERROR("Mesh table full (%u/%u)", ctx->mesh_count, MAX_MESHES);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Upload vertices */
    VkDeviceSize vert_size   = sizeof(Vertex3D) * vertex_count;
    VkDeviceSize vert_offset = sizeof(Vertex3D) * ctx->vertex_3d_total;

    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    EngineResult res = vk_create_buffer(ctx, vert_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buf, &staging_mem);
    if (res != ENGINE_SUCCESS) return res;

    void *mapped;
    vkMapMemory(ctx->device, staging_mem, 0, vert_size, 0, &mapped);
    memcpy(mapped, vertices, (size_t)vert_size);
    vkUnmapMemory(ctx->device, staging_mem);

    VkCommandBuffer cmd = vk_begin_single_command(ctx);
    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = vert_offset,
        .size      = vert_size,
    };
    vkCmdCopyBuffer(cmd, staging_buf, ctx->vertex_buffer_3d, 1, &copy_region);
    vk_end_single_command(ctx, cmd);

    vkDestroyBuffer(ctx->device, staging_buf, NULL);
    vkFreeMemory(ctx->device, staging_mem, NULL);

    /* Upload indices (if any) */
    u32 first_index = 0;
    if (indices && index_count > 0) {
        VkDeviceSize idx_size   = sizeof(u32) * index_count;
        VkDeviceSize idx_offset = sizeof(u32) * ctx->index_total;

        res = vk_create_buffer(ctx, idx_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging_buf, &staging_mem);
        if (res != ENGINE_SUCCESS) return res;

        vkMapMemory(ctx->device, staging_mem, 0, idx_size, 0, &mapped);
        memcpy(mapped, indices, (size_t)idx_size);
        vkUnmapMemory(ctx->device, staging_mem);

        cmd = vk_begin_single_command(ctx);
        VkBufferCopy idx_copy = {
            .srcOffset = 0,
            .dstOffset = idx_offset,
            .size      = idx_size,
        };
        vkCmdCopyBuffer(cmd, staging_buf, ctx->index_buffer, 1, &idx_copy);
        vk_end_single_command(ctx, cmd);

        vkDestroyBuffer(ctx->device, staging_buf, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);

        first_index = ctx->index_total;
        ctx->index_total += index_count;
    }

    /* Register mesh slot */
    MeshHandle handle = (MeshHandle)ctx->mesh_count;
    ctx->meshes[handle].first_vertex = ctx->vertex_3d_total;
    ctx->meshes[handle].vertex_count = vertex_count;
    ctx->meshes[handle].is_3d        = true;
    ctx->meshes[handle].first_index  = first_index;
    ctx->meshes[handle].index_count  = index_count;
    ctx->vertex_3d_total += vertex_count;
    ctx->mesh_count++;

    *out_handle = handle;

    LOG_INFO("3D mesh %u uploaded: %u vertices, %u indices",
             handle, vertex_count, index_count);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Skinned vertex buffer (pre-allocated GPU-local, separate from regular 3D)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_vertex_buffer_skinned(VulkanContext *ctx, u32 max_vertices) {
    VkDeviceSize buf_size = sizeof(SkinnedVertex3D) * max_vertices;

    EngineResult res = vk_create_buffer(ctx, buf_size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &ctx->vertex_buffer_skinned, &ctx->vertex_buffer_skinned_memory);
    if (res != ENGINE_SUCCESS) return res;

    ctx->vertex_skinned_total = 0;

    LOG_INFO("Skinned vertex buffer created: capacity %u vertices (%llu bytes)",
             max_vertices, (unsigned long long)buf_size);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Skinned mesh upload (SkinnedVertex3D + indices via staging)
 * ------------------------------------------------------------------------ */

EngineResult vk_upload_mesh_skinned(VulkanContext *ctx,
                                     const SkinnedVertex3D *vertices, u32 vertex_count,
                                     const u32 *indices, u32 index_count,
                                     MeshHandle *out_handle)
{
    if (ctx->mesh_count >= MAX_MESHES) {
        LOG_ERROR("Mesh table full (%u/%u)", ctx->mesh_count, MAX_MESHES);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Upload vertices */
    VkDeviceSize vert_size   = sizeof(SkinnedVertex3D) * vertex_count;
    VkDeviceSize vert_offset = sizeof(SkinnedVertex3D) * ctx->vertex_skinned_total;

    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    EngineResult res = vk_create_buffer(ctx, vert_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buf, &staging_mem);
    if (res != ENGINE_SUCCESS) return res;

    void *mapped;
    vkMapMemory(ctx->device, staging_mem, 0, vert_size, 0, &mapped);
    memcpy(mapped, vertices, (size_t)vert_size);
    vkUnmapMemory(ctx->device, staging_mem);

    VkCommandBuffer cmd = vk_begin_single_command(ctx);
    VkBufferCopy copy_region = {
        .srcOffset = 0,
        .dstOffset = vert_offset,
        .size      = vert_size,
    };
    vkCmdCopyBuffer(cmd, staging_buf, ctx->vertex_buffer_skinned, 1, &copy_region);
    vk_end_single_command(ctx, cmd);

    vkDestroyBuffer(ctx->device, staging_buf, NULL);
    vkFreeMemory(ctx->device, staging_mem, NULL);

    /* Upload indices (shared index buffer with regular 3D meshes) */
    u32 first_index = 0;
    if (indices && index_count > 0) {
        VkDeviceSize idx_size   = sizeof(u32) * index_count;
        VkDeviceSize idx_offset = sizeof(u32) * ctx->index_total;

        res = vk_create_buffer(ctx, idx_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &staging_buf, &staging_mem);
        if (res != ENGINE_SUCCESS) return res;

        vkMapMemory(ctx->device, staging_mem, 0, idx_size, 0, &mapped);
        memcpy(mapped, indices, (size_t)idx_size);
        vkUnmapMemory(ctx->device, staging_mem);

        cmd = vk_begin_single_command(ctx);
        VkBufferCopy idx_copy = {
            .srcOffset = 0,
            .dstOffset = idx_offset,
            .size      = idx_size,
        };
        vkCmdCopyBuffer(cmd, staging_buf, ctx->index_buffer, 1, &idx_copy);
        vk_end_single_command(ctx, cmd);

        vkDestroyBuffer(ctx->device, staging_buf, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);

        first_index = ctx->index_total;
        ctx->index_total += index_count;
    }

    /* Register mesh slot */
    MeshHandle handle = (MeshHandle)ctx->mesh_count;
    ctx->meshes[handle].first_vertex = ctx->vertex_skinned_total;
    ctx->meshes[handle].vertex_count = vertex_count;
    ctx->meshes[handle].is_3d        = true;
    ctx->meshes[handle].is_skinned   = true;
    ctx->meshes[handle].first_index  = first_index;
    ctx->meshes[handle].index_count  = index_count;
    ctx->vertex_skinned_total += vertex_count;
    ctx->mesh_count++;

    *out_handle = handle;

    LOG_INFO("Skinned mesh %u uploaded: %u vertices, %u indices",
             handle, vertex_count, index_count);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Texture creation (staging -> GPU-local VkImage)
 * ------------------------------------------------------------------------ */

static void transition_image_layout(VulkanContext *ctx, VkImage image,
                                     VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkCommandBuffer cmd = vk_begin_single_command(ctx);

    VkImageMemoryBarrier barrier = {
        .sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout           = old_layout,
        .newLayout           = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image               = image,
        .subresourceRange    = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    VkPipelineStageFlags src_stage, dst_stage;

    if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED &&
        new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else {
        LOG_ERROR("Unsupported layout transition");
        vk_end_single_command(ctx, cmd);
        return;
    }

    vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                          0, NULL, 0, NULL, 1, &barrier);

    vk_end_single_command(ctx, cmd);
}

EngineResult vk_create_texture(VulkanContext *ctx,
                               const u8 *pixels,
                               u32 width, u32 height,
                               VkFormat format,
                               VkFilter filter,
                               VulkanTexture *out_tex)
{
    u32 pixel_size = (format == VK_FORMAT_R8_UNORM) ? 1 : 4;
    VkDeviceSize image_size = (VkDeviceSize)width * height * pixel_size;

    /* Staging buffer */
    VkBuffer staging_buf;
    VkDeviceMemory staging_mem;
    EngineResult res = vk_create_buffer(ctx, image_size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &staging_buf, &staging_mem);
    if (res != ENGINE_SUCCESS) return res;

    void *mapped;
    vkMapMemory(ctx->device, staging_mem, 0, image_size, 0, &mapped);
    memcpy(mapped, pixels, (size_t)image_size);
    vkUnmapMemory(ctx->device, staging_mem);

    /* Create VkImage */
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent        = { width, height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .format        = format,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
    };

    if (vkCreateImage(ctx->device, &image_info, NULL, &out_tex->image) != VK_SUCCESS) {
        LOG_ERROR("Failed to create texture image");
        vkDestroyBuffer(ctx->device, staging_buf, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Allocate memory for image */
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, out_tex->image, &mem_reqs);

    u32 mem_type = find_memory_type(ctx, mem_reqs.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(ctx->device, out_tex->image, NULL);
        vkDestroyBuffer(ctx->device, staging_buf, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, &out_tex->memory) != VK_SUCCESS) {
        vkDestroyImage(ctx->device, out_tex->image, NULL);
        vkDestroyBuffer(ctx->device, staging_buf, NULL);
        vkFreeMemory(ctx->device, staging_mem, NULL);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    vkBindImageMemory(ctx->device, out_tex->image, out_tex->memory, 0);

    /* Transition to transfer dst, copy, transition to shader read */
    transition_image_layout(ctx, out_tex->image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBuffer cmd = vk_begin_single_command(ctx);
    VkBufferImageCopy region = {
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .layerCount = 1,
        },
        .imageExtent = { width, height, 1 },
    };
    vkCmdCopyBufferToImage(cmd, staging_buf, out_tex->image,
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vk_end_single_command(ctx, cmd);

    transition_image_layout(ctx, out_tex->image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /* Cleanup staging */
    vkDestroyBuffer(ctx->device, staging_buf, NULL);
    vkFreeMemory(ctx->device, staging_mem, NULL);

    /* Image view */
    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = out_tex->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &view_info, NULL, &out_tex->view) != VK_SUCCESS) {
        LOG_ERROR("Failed to create texture image view");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Sampler — filter passed from caller (NEAREST for pixel art, LINEAR for smooth) */
    VkSamplerMipmapMode mip_mode = (filter == VK_FILTER_NEAREST)
        ? VK_SAMPLER_MIPMAP_MODE_NEAREST
        : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    VkSamplerCreateInfo sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = filter,
        .minFilter    = filter,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode   = mip_mode,
    };

    if (vkCreateSampler(ctx->device, &sampler_info, NULL, &out_tex->sampler) != VK_SUCCESS) {
        LOG_ERROR("Failed to create texture sampler");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    out_tex->width  = width;
    out_tex->height = height;

    LOG_INFO("Texture created: %ux%u", width, height);
    return ENGINE_SUCCESS;
}

void vk_destroy_texture(VulkanContext *ctx, VulkanTexture *tex) {
    if (tex->sampler)  vkDestroySampler(ctx->device, tex->sampler, NULL);
    if (tex->view)     vkDestroyImageView(ctx->device, tex->view, NULL);
    if (tex->image)    vkDestroyImage(ctx->device, tex->image, NULL);
    if (tex->memory)   vkFreeMemory(ctx->device, tex->memory, NULL);
    memset(tex, 0, sizeof(*tex));
}
