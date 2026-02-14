#include "renderer/text.h"
#include "renderer/vk_buffer.h"
#include "core/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stb_truetype implementation */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

/* --------------------------------------------------------------------------
 * Font atlas data (module-level state)
 * ------------------------------------------------------------------------ */

#define ATLAS_WIDTH  512
#define ATLAS_HEIGHT 512
#define FIRST_CHAR   32  /* space */
#define CHAR_COUNT   96  /* printable ASCII: 32..127 */

static stbtt_bakedchar s_char_data[CHAR_COUNT];
static f32             s_font_size;

/* Per-frame text vertex staging */
static TextVertex s_vertices[TEXT_MAX_CHARS * 6]; /* 6 verts per char (2 tris) */
static u32        s_vertex_count;

/* --------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

EngineResult text_init(VulkanContext *ctx, const char *font_path, f32 font_size) {
    s_font_size = font_size;
    s_vertex_count = 0;

    /* Read font file */
    FILE *f = fopen(font_path, "rb");
    if (!f) {
        LOG_FATAL("Failed to open font: %s", font_path);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *font_data = malloc((size_t)fsize);
    if (!font_data) { fclose(f); return ENGINE_ERROR_OUT_OF_MEMORY; }
    fread(font_data, 1, (size_t)fsize, f);
    fclose(f);

    /* Bake font atlas */
    u8 *atlas_bitmap = malloc(ATLAS_WIDTH * ATLAS_HEIGHT);
    if (!atlas_bitmap) { free(font_data); return ENGINE_ERROR_OUT_OF_MEMORY; }

    int bake_result = stbtt_BakeFontBitmap(font_data, 0, font_size,
        atlas_bitmap, ATLAS_WIDTH, ATLAS_HEIGHT,
        FIRST_CHAR, CHAR_COUNT, s_char_data);

    free(font_data);

    if (bake_result <= 0) {
        LOG_WARN("Font atlas may be too small (bake returned %d), but continuing", bake_result);
    }

    /* Upload atlas as R8 texture */
    EngineResult res = vk_create_texture(ctx, atlas_bitmap, ATLAS_WIDTH, ATLAS_HEIGHT,
                                          VK_FORMAT_R8_UNORM, &ctx->font_atlas);
    free(atlas_bitmap);
    if (res != ENGINE_SUCCESS) return res;

    /* Descriptor pool */
    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
        .maxSets       = 1,
    };

    if (vkCreateDescriptorPool(ctx->device, &pool_info, NULL,
                                &ctx->text_desc_pool) != VK_SUCCESS) {
        LOG_FATAL("Failed to create text descriptor pool");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Allocate descriptor set */
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = ctx->text_desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &ctx->text_desc_set_layout,
    };

    if (vkAllocateDescriptorSets(ctx->device, &alloc_info,
                                  &ctx->text_desc_set) != VK_SUCCESS) {
        LOG_FATAL("Failed to allocate text descriptor set");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Write descriptor (bind font atlas) */
    VkDescriptorImageInfo image_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = ctx->font_atlas.view,
        .sampler     = ctx->font_atlas.sampler,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = ctx->text_desc_set,
        .dstBinding      = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo      = &image_info,
    };

    vkUpdateDescriptorSets(ctx->device, 1, &write, 0, NULL);

    /* Create text vertex buffer (CPU-visible, updated every frame) */
    VkDeviceSize buf_size = sizeof(TextVertex) * TEXT_MAX_CHARS * 6;
    ctx->text_vertex_capacity = TEXT_MAX_CHARS * 6;

    res = vk_create_buffer(ctx, buf_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &ctx->text_vertex_buffer, &ctx->text_vertex_buffer_memory);
    if (res != ENGINE_SUCCESS) return res;

    /* Persistently map — avoids vkMapMemory/vkUnmapMemory every frame */
    if (vkMapMemory(ctx->device, ctx->text_vertex_buffer_memory,
                    0, buf_size, 0, &ctx->text_vertex_mapped) != VK_SUCCESS) {
        LOG_FATAL("Failed to persistently map text vertex buffer");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    LOG_INFO("Text rendering initialized (font: %s, size: %.0f, atlas: %dx%d)",
             font_path, font_size, ATLAS_WIDTH, ATLAS_HEIGHT);
    return ENGINE_SUCCESS;
}

void text_draw(VulkanContext *ctx, const char *str,
               f32 x, f32 y, f32 scale, f32 r, f32 g, f32 b)
{
    ENGINE_UNUSED(ctx);

    /* stb_truetype works at the baked font size.
     * We let it advance cursor_x/cursor_y at native (1x) size,
     * then scale the resulting quad positions relative to the
     * requested origin (x, y). */
    f32 cursor_x = x / scale;
    f32 cursor_y = (y + s_font_size * scale) / scale; /* baseline offset */

    for (const char *p = str; *p; p++) {
        int ch = (int)(unsigned char)*p;
        if (ch < FIRST_CHAR || ch >= FIRST_CHAR + CHAR_COUNT) continue;

        if (s_vertex_count + 6 > TEXT_MAX_CHARS * 6) {
            LOG_WARN("Text vertex limit reached, skipping remaining characters");
            break;
        }

        stbtt_aligned_quad q;
        stbtt_GetBakedQuad(s_char_data, ATLAS_WIDTH, ATLAS_HEIGHT,
                           ch - FIRST_CHAR, &cursor_x, &cursor_y, &q, 1);

        /* Scale quad positions from native size to requested size */
        f32 x0 = q.x0 * scale;
        f32 y0 = q.y0 * scale;
        f32 x1 = q.x1 * scale;
        f32 y1 = q.y1 * scale;

        /* Two triangles per character quad */
        TextVertex *v = &s_vertices[s_vertex_count];

        /* Triangle 1: top-left, bottom-left, bottom-right */
        v[0] = (TextVertex){ .position = { x0, y0 }, .uv = { q.s0, q.t0 }, .color = { r, g, b } };
        v[1] = (TextVertex){ .position = { x0, y1 }, .uv = { q.s0, q.t1 }, .color = { r, g, b } };
        v[2] = (TextVertex){ .position = { x1, y1 }, .uv = { q.s1, q.t1 }, .color = { r, g, b } };

        /* Triangle 2: top-left, bottom-right, top-right */
        v[3] = (TextVertex){ .position = { x0, y0 }, .uv = { q.s0, q.t0 }, .color = { r, g, b } };
        v[4] = (TextVertex){ .position = { x1, y1 }, .uv = { q.s1, q.t1 }, .color = { r, g, b } };
        v[5] = (TextVertex){ .position = { x1, y0 }, .uv = { q.s1, q.t0 }, .color = { r, g, b } };

        s_vertex_count += 6;
    }
}

void text_flush(VulkanContext *ctx, VkCommandBuffer cmd) {
    if (s_vertex_count == 0) return;

    /* Upload vertex data via persistently mapped pointer (no map/unmap overhead) */
    memcpy(ctx->text_vertex_mapped, s_vertices, sizeof(TextVertex) * s_vertex_count);

    ctx->text_vertex_count = s_vertex_count;

    /* Bind text pipeline */
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx->text_pipeline);

    /* Push screen size */
    f32 screen_size[2] = {
        (f32)ctx->swapchain_extent.width,
        (f32)ctx->swapchain_extent.height,
    };
    vkCmdPushConstants(cmd, ctx->text_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen_size), screen_size);

    /* Bind descriptor set (font atlas) */
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            ctx->text_pipeline_layout, 0, 1,
                            &ctx->text_desc_set, 0, NULL);

    /* Bind text vertex buffer */
    VkBuffer buffers[] = { ctx->text_vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);

    /* Draw */
    vkCmdDraw(cmd, ctx->text_vertex_count, 1, 0, 0);

    /* Reset for next frame */
    s_vertex_count = 0;
}

void text_shutdown(VulkanContext *ctx) {
    vkDeviceWaitIdle(ctx->device);

    if (ctx->text_vertex_buffer) {
        if (ctx->text_vertex_mapped) {
            vkUnmapMemory(ctx->device, ctx->text_vertex_buffer_memory);
            ctx->text_vertex_mapped = NULL;
        }
        vkDestroyBuffer(ctx->device, ctx->text_vertex_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->text_vertex_buffer_memory, NULL);
        ctx->text_vertex_buffer = VK_NULL_HANDLE;
    }

    if (ctx->text_desc_pool) {
        vkDestroyDescriptorPool(ctx->device, ctx->text_desc_pool, NULL);
        ctx->text_desc_pool = VK_NULL_HANDLE;
    }

    vk_destroy_texture(ctx, &ctx->font_atlas);

    LOG_INFO("Text rendering shut down");
}
