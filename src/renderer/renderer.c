#include "renderer/vk_types.h"
#include "renderer/renderer.h"
#include "renderer/vk_init.h"
#include "renderer/vk_pipeline.h"
#include "renderer/vk_buffer.h"
#include "renderer/bloom.h"
#include "renderer/text.h"
#include "renderer/skinned_model.h"
#include "platform/window.h"
#include "core/log.h"

#include <cglm/mat4.h>
#include <cglm/cam.h>
#include <cglm/affine.h>

#include "stb/stb_image.h"

#include <stdlib.h>
#include <string.h>

#define MAX_INSTANCES              4096
#define MAX_VERTICES               65536
#define CAMERA_DEFAULT_HALF_HEIGHT 10.0f

struct Renderer {
    VulkanContext vk;
    Window       *window;
    u32           current_image_index; /* set by begin_frame, used by end_frame */
    f32           clear_color[4];      /* r, g, b, a */
    BloomSettings bloom_settings;      /* current bloom config */
};

/* --------------------------------------------------------------------------
 * Swapchain recreation (for resize)
 * ------------------------------------------------------------------------ */

static EngineResult recreate_swapchain(Renderer *r) {
    i32 width = 0, height = 0;
    window_get_framebuffer_size(r->window, &width, &height);

    /* Wait while minimized */
    while (width == 0 || height == 0) {
        window_get_framebuffer_size(r->window, &width, &height);
        window_poll_events();
    }

    vkDeviceWaitIdle(r->vk.device);

    /* Destroy bloom resources that depend on swapchain image views FIRST */
    if (r->vk.bloom.enabled) {
        bloom_cleanup_swapchain_deps(&r->vk);
    }

    vk_cleanup_swapchain(&r->vk);

    EngineResult res;
    if ((res = vk_create_swapchain(&r->vk, width, height)) != ENGINE_SUCCESS) return res;
    if ((res = vk_create_image_views(&r->vk))               != ENGINE_SUCCESS) return res;
    if ((res = vk_create_depth_resources(&r->vk))            != ENGINE_SUCCESS) return res;
    if ((res = vk_create_framebuffers(&r->vk))               != ENGINE_SUCCESS) return res;

    /* Recreate bloom size-dependent resources */
    if (r->vk.bloom.enabled) {
        if ((res = bloom_resize(&r->vk)) != ENGINE_SUCCESS) return res;
    }

    LOG_INFO("Swapchain recreated: %dx%d", width, height);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Helper: record geometry draw commands into a command buffer.
 * Used by both the bloom path and the non-bloom path.
 * ------------------------------------------------------------------------ */

static void record_geometry_draws(VulkanContext *vk, VkCommandBuffer cmd, VkPipeline geo_pipeline) {
    if (vk->draw_command_count == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, geo_pipeline);

    /* Bind vertex buffer (binding 0) and instance buffer (binding 1) */
    VkBuffer buffers[] = { vk->vertex_buffer, vk->instance_buffer };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);

    for (u32 i = 0; i < vk->draw_command_count; i++) {
        DrawCommand *dc = &vk->draw_commands[i];
        MeshSlot *mesh  = &vk->meshes[dc->mesh];

        /* Push VP matrix + use_texture flag (68 bytes total) */
        struct { float vp[16]; u32 use_texture; } push_data;
        memcpy(push_data.vp, vk->vp_matrix, 64);
        push_data.use_texture = (dc->texture != TEXTURE_HANDLE_INVALID) ? 1 : 0;
        vkCmdPushConstants(cmd, vk->pipeline_layout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 68, &push_data);

        /* Bind texture descriptor (real texture or dummy for untextured) */
        VkDescriptorSet desc_set = vk->dummy_desc_set;
        if (dc->texture != TEXTURE_HANDLE_INVALID && dc->texture < vk->texture_count) {
            desc_set = vk->texture_desc_sets[dc->texture];
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout, 0, 1,
                                 &desc_set, 0, NULL);

        vkCmdDraw(cmd,
                  mesh->vertex_count,   /* vertexCount */
                  dc->instance_count,   /* instanceCount */
                  mesh->first_vertex,   /* firstVertex */
                  dc->instance_offset); /* firstInstance */
    }
}

/* --------------------------------------------------------------------------
 * Helper: record 3D geometry draw commands into a command buffer.
 * ------------------------------------------------------------------------ */

static void record_geometry_draws_3d(VulkanContext *vk, VkCommandBuffer cmd, VkPipeline pipeline_3d) {
    if (vk->draw_command_3d_count == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_3d);

    /* Bind 3D vertex buffer (binding 0) and 3D instance buffer (binding 1) */
    VkBuffer buffers[] = { vk->vertex_buffer_3d, vk->instance_buffer_3d };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);

    /* Bind index buffer */
    if (vk->index_buffer) {
        vkCmdBindIndexBuffer(cmd, vk->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    for (u32 i = 0; i < vk->draw_command_3d_count; i++) {
        DrawCommand *dc = &vk->draw_commands_3d[i];
        MeshSlot *mesh  = &vk->meshes[dc->mesh];

        /* Push VP matrix + use_texture flag (68 bytes total) */
        struct { float vp[16]; u32 use_texture; } push_data;
        memcpy(push_data.vp, vk->vp_matrix, 64);
        push_data.use_texture = (dc->texture != TEXTURE_HANDLE_INVALID) ? 1 : 0;
        vkCmdPushConstants(cmd, vk->pipeline_layout_3d,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 68, &push_data);

        /* Bind texture descriptor (set 0) */
        VkDescriptorSet tex_set = vk->dummy_desc_set;
        if (dc->texture != TEXTURE_HANDLE_INVALID && dc->texture < vk->texture_count) {
            tex_set = vk->texture_desc_sets[dc->texture];
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout_3d, 0, 1,
                                 &tex_set, 0, NULL);

        /* Bind light UBO descriptor (set 1) */
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout_3d, 1, 1,
                                 &vk->light_desc_set, 0, NULL);

        if (mesh->index_count > 0) {
            vkCmdDrawIndexed(cmd,
                             mesh->index_count,
                             dc->instance_count,
                             mesh->first_index,
                             (i32)mesh->first_vertex,
                             dc->instance_offset);
        } else {
            vkCmdDraw(cmd,
                      mesh->vertex_count,
                      dc->instance_count,
                      mesh->first_vertex,
                      dc->instance_offset);
        }
    }
}

/* --------------------------------------------------------------------------
 * Helper: record skinned 3D geometry draw commands into a command buffer.
 * ------------------------------------------------------------------------ */

static void record_geometry_draws_skinned(VulkanContext *vk, VkCommandBuffer cmd,
                                           VkPipeline skinned_pipeline) {
    if (vk->draw_command_skinned_count == 0) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skinned_pipeline);

    /* Bind skinned vertex buffer (binding 0) and skinned instance buffer (binding 1) */
    VkBuffer buffers[] = { vk->vertex_buffer_skinned, vk->instance_buffer_skinned };
    VkDeviceSize offsets[] = { 0, 0 };
    vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);

    /* Bind index buffer (shared with regular 3D meshes) */
    if (vk->index_buffer) {
        vkCmdBindIndexBuffer(cmd, vk->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    for (u32 i = 0; i < vk->draw_command_skinned_count; i++) {
        SkinnedDrawCommand *dc = &vk->draw_commands_skinned[i];
        MeshSlot *mesh = &vk->meshes[dc->mesh];

        /* Push constants: VP (64B) + use_texture (4B) + joint_offset (4B) + joint_count (4B) = 76B */
        struct {
            float vp[16];
            u32 use_texture;
            u32 joint_offset;
            u32 joint_count;
        } push_data;
        memcpy(push_data.vp, vk->vp_matrix, 64);
        push_data.use_texture = (dc->texture != TEXTURE_HANDLE_INVALID) ? 1 : 0;
        push_data.joint_offset = dc->joint_ssbo_offset;
        push_data.joint_count = dc->joint_count;
        vkCmdPushConstants(cmd, vk->pipeline_layout_skinned,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 76, &push_data);

        /* Bind texture descriptor (set 0) */
        VkDescriptorSet tex_set = vk->dummy_desc_set;
        if (dc->texture != TEXTURE_HANDLE_INVALID && dc->texture < vk->texture_count) {
            tex_set = vk->texture_desc_sets[dc->texture];
        }
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout_skinned, 0, 1,
                                 &tex_set, 0, NULL);

        /* Bind light UBO descriptor (set 1) */
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout_skinned, 1, 1,
                                 &vk->light_desc_set, 0, NULL);

        /* Bind joint SSBO descriptor (set 2) */
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 vk->pipeline_layout_skinned, 2, 1,
                                 &vk->joint_desc_set, 0, NULL);

        if (mesh->index_count > 0) {
            vkCmdDrawIndexed(cmd,
                             mesh->index_count,
                             dc->instance_count,
                             mesh->first_index,
                             (i32)mesh->first_vertex,
                             dc->instance_offset);
        } else {
            vkCmdDraw(cmd,
                      mesh->vertex_count,
                      dc->instance_count,
                      mesh->first_vertex,
                      dc->instance_offset);
        }
    }
}

/* --------------------------------------------------------------------------
 * Command buffer recording
 * ------------------------------------------------------------------------ */

static EngineResult record_command_buffer(Renderer *renderer, VkCommandBuffer cmd, u32 image_index) {
    VulkanContext *vk = &renderer->vk;

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    if (vkBeginCommandBuffer(cmd, &begin_info) != VK_SUCCESS) {
        LOG_ERROR("Failed to begin recording command buffer");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    if (vk->bloom.enabled) {
        /* ================================================================
         * BLOOM PATH: Render scene to offscreen HDR, then post-process
         * ================================================================ */

        VkClearValue clear_values[2];
        clear_values[0].color = (VkClearColorValue){{
            vk->clear_color[0], vk->clear_color[1],
            vk->clear_color[2], vk->clear_color[3]
        }};
        clear_values[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };

        /* Pass 1: Scene -> offscreen HDR image */
        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = vk->bloom.scene_render_pass,
            .framebuffer = vk->bloom.scene_framebuffer,
            .renderArea  = { .offset = {0, 0}, .extent = vk->swapchain_extent },
            .clearValueCount = 2,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        /* Dynamic viewport & scissor */
        VkViewport viewport = {
            0.0f, 0.0f,
            (float)vk->swapchain_extent.width, (float)vk->swapchain_extent.height,
            0.0f, 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = { .offset = {0, 0}, .extent = vk->swapchain_extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        /* Draw 2D geometry using bloom scene pipeline */
        record_geometry_draws(vk, cmd, vk->bloom.scene_graphics_pipeline);

        /* Draw 3D geometry using bloom scene 3D pipeline */
        record_geometry_draws_3d(vk, cmd, vk->bloom.scene_3d_pipeline);

        /* Draw skinned 3D geometry using bloom scene skinned pipeline */
        record_geometry_draws_skinned(vk, cmd, vk->bloom.scene_skinned_pipeline);

        /* Draw text using bloom scene text pipeline */
        text_flush_with_pipeline(vk, cmd, vk->bloom.scene_text_pipeline);

        vkCmdEndRenderPass(cmd);

        /* Passes 2-5: extract, blur, composite */
        bloom_record(vk, cmd, &renderer->bloom_settings, image_index);

    } else {
        /* ================================================================
         * ORIGINAL PATH: Single render pass directly to swapchain
         * ================================================================ */

        VkClearValue clear_values[2];
        clear_values[0].color = (VkClearColorValue){{
            vk->clear_color[0], vk->clear_color[1],
            vk->clear_color[2], vk->clear_color[3]
        }};
        clear_values[1].depthStencil = (VkClearDepthStencilValue){ 1.0f, 0 };

        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = vk->render_pass,
            .framebuffer = vk->framebuffers[image_index],
            .renderArea  = { .offset = {0, 0}, .extent = vk->swapchain_extent },
            .clearValueCount = 2,
            .pClearValues    = clear_values,
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {
            0.0f, 0.0f,
            (float)vk->swapchain_extent.width, (float)vk->swapchain_extent.height,
            0.0f, 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor = { .offset = {0, 0}, .extent = vk->swapchain_extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        /* Draw 2D geometry using standard pipeline */
        record_geometry_draws(vk, cmd, vk->graphics_pipeline);

        /* Draw 3D geometry using 3D pipeline */
        record_geometry_draws_3d(vk, cmd, vk->graphics_pipeline_3d);

        /* Draw skinned 3D geometry using skinned pipeline */
        record_geometry_draws_skinned(vk, cmd, vk->graphics_pipeline_skinned);

        /* Draw text overlay */
        text_flush(vk, cmd);

        vkCmdEndRenderPass(cmd);
    }

    if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        LOG_ERROR("Failed to record command buffer");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Camera VP matrix computation
 * ------------------------------------------------------------------------ */

static void compute_vp_matrix(VulkanContext *vk, const Camera2D *camera) {
    f32 aspect = (f32)vk->swapchain_extent.width / (f32)vk->swapchain_extent.height;
    f32 base_h = (camera->half_height > 0.0f) ? camera->half_height : CAMERA_DEFAULT_HALF_HEIGHT;
    f32 zoom   = (camera->zoom > 0.0f) ? camera->zoom : 1.0f;
    f32 half_h = base_h / zoom;
    f32 half_w = half_h * aspect;

    /* Projection: orthographic, Y-down (Vulkan convention: top = -half_h, bottom = +half_h) */
    mat4 proj;
    glm_ortho(-half_w, half_w, half_h, -half_h, -1.0f, 1.0f, proj);

    /* View: translate then rotate around the camera center */
    mat4 view;
    glm_mat4_identity(view);

    /* Rotate view (negative = inverse of camera rotation) */
    if (camera->rotation != 0.0f) {
        glm_rotate_z(view, -camera->rotation, view);
    }

    /* Translate view (negative = inverse of camera position) */
    vec3 translate = { -camera->position[0], -camera->position[1], 0.0f };
    glm_translate(view, translate);

    /* VP = projection x view */
    mat4 vp;
    glm_mat4_mul(proj, view, vp);

    /* Store as flat float[16] for push constants */
    memcpy(vk->vp_matrix, vp, sizeof(float) * 16);
}

static void compute_vp_matrix_3d(VulkanContext *vk, const Camera3D *camera) {
    f32 aspect = (f32)vk->swapchain_extent.width / (f32)vk->swapchain_extent.height;
    f32 fov_rad = camera->fov * ((f32)GLM_PI / 180.0f);

    mat4 proj;
    glm_perspective(fov_rad, aspect, camera->near_plane, camera->far_plane, proj);

    /* Vulkan Y-flip: cglm follows OpenGL convention (Y up), Vulkan has Y down */
    proj[1][1] *= -1.0f;

    mat4 view;
    vec3 eye    = { camera->position[0], camera->position[1], camera->position[2] };
    vec3 center = { camera->target[0],   camera->target[1],   camera->target[2] };
    vec3 up     = { camera->up[0],       camera->up[1],       camera->up[2] };
    glm_lookat(eye, center, up, view);

    mat4 vp;
    glm_mat4_mul(proj, view, vp);

    memcpy(vk->vp_matrix, vp, sizeof(float) * 16);

    /* Cache camera position for specular lighting */
    vk->view_position[0] = camera->position[0];
    vk->view_position[1] = camera->position[1];
    vk->view_position[2] = camera->position[2];
}

/* --------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

EngineResult renderer_create(Window *window, const RendererConfig *config,
                             Renderer **out_renderer) {
    Renderer *r = calloc(1, sizeof(Renderer));
    if (!r) return ENGINE_ERROR_OUT_OF_MEMORY;

    r->window = window;

    /* Default bloom settings */
    BloomSettings default_bloom = BLOOM_SETTINGS_DEFAULT;
    r->bloom_settings = default_bloom;

    /* Clear color: use config if any channel is non-zero, else default dark grey */
    if (config->clear_color[0] != 0.0f || config->clear_color[1] != 0.0f ||
        config->clear_color[2] != 0.0f || config->clear_color[3] != 0.0f) {
        memcpy(r->vk.clear_color, config->clear_color, sizeof(f32) * 4);
    } else {
        r->vk.clear_color[0] = 0.1f;
        r->vk.clear_color[1] = 0.1f;
        r->vk.clear_color[2] = 0.12f;
        r->vk.clear_color[3] = 1.0f;
    }

    i32 width, height;
    window_get_framebuffer_size(window, &width, &height);

    EngineResult res;

    /* Vulkan initialization sequence */
    if ((res = vk_create_instance(&r->vk))          != ENGINE_SUCCESS) goto fail;
    if ((res = vk_setup_debug_messenger(&r->vk))     != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_surface(&r->vk, window))    != ENGINE_SUCCESS) goto fail;
    if ((res = vk_pick_physical_device(&r->vk))      != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_logical_device(&r->vk))     != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_swapchain(&r->vk, width, height)) != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_image_views(&r->vk))        != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_render_pass(&r->vk))        != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_graphics_pipeline(&r->vk))  != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_text_pipeline(&r->vk))      != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_depth_resources(&r->vk))    != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_framebuffers(&r->vk))       != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_command_pool(&r->vk))       != ENGINE_SUCCESS) goto fail;

    /* Shared vertex buffer (pre-allocated, meshes appended via staging) */
    if ((res = vk_create_vertex_buffer(&r->vk, MAX_VERTICES)) != ENGINE_SUCCESS) goto fail;

    /* Text rendering init */
    if ((res = text_init(&r->vk, config->font_path, config->font_size)) != ENGINE_SUCCESS) goto fail;

    /* Instance buffer (CPU-visible, persistently mapped, updated every frame) */
    {
        VkDeviceSize buf_size = sizeof(InstanceData) * MAX_INSTANCES;
        r->vk.instance_capacity = MAX_INSTANCES;
        r->vk.instance_count = 0;

        res = vk_create_buffer(&r->vk, buf_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->vk.instance_buffer, &r->vk.instance_buffer_memory);
        if (res != ENGINE_SUCCESS) goto fail;

        if (vkMapMemory(r->vk.device, r->vk.instance_buffer_memory,
                        0, buf_size, 0, &r->vk.instance_mapped) != VK_SUCCESS) {
            LOG_FATAL("Failed to persistently map instance buffer");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }
    }

    /* 1x1 white dummy texture — bound for untextured draws so the descriptor
     * set 0 (sampler) is always valid, even when the shader doesn't sample it. */
    {
        u8 white_pixel[] = { 255, 255, 255, 255 };
        res = vk_create_texture(&r->vk, white_pixel, 1, 1,
                                VK_FORMAT_R8G8B8A8_SRGB, VK_FILTER_NEAREST,
                                &r->vk.dummy_texture);
        if (res != ENGINE_SUCCESS) goto fail;

        VkDescriptorSetAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = r->vk.geo_desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &r->vk.geo_desc_set_layout,
        };
        if (vkAllocateDescriptorSets(r->vk.device, &alloc_info,
                                      &r->vk.dummy_desc_set) != VK_SUCCESS) {
            LOG_FATAL("Failed to allocate dummy descriptor set");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        VkDescriptorImageInfo img_info = {
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .imageView   = r->vk.dummy_texture.view,
            .sampler     = r->vk.dummy_texture.sampler,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = r->vk.dummy_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &img_info,
        };
        vkUpdateDescriptorSets(r->vk.device, 1, &write, 0, NULL);
    }

    /* ---- 3D pipeline and buffers ---- */
    if ((res = vk_create_3d_pipeline(&r->vk)) != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_vertex_buffer_3d(&r->vk, MAX_VERTICES_3D)) != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_index_buffer(&r->vk, MAX_INDICES)) != ENGINE_SUCCESS) goto fail;

    /* 3D instance buffer (CPU-visible, persistently mapped) */
    {
        VkDeviceSize buf_size = sizeof(InstanceData3D) * MAX_INSTANCES;
        r->vk.instance_3d_capacity = MAX_INSTANCES;
        r->vk.instance_3d_count = 0;

        res = vk_create_buffer(&r->vk, buf_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->vk.instance_buffer_3d, &r->vk.instance_buffer_3d_memory);
        if (res != ENGINE_SUCCESS) goto fail;

        if (vkMapMemory(r->vk.device, r->vk.instance_buffer_3d_memory,
                        0, buf_size, 0, &r->vk.instance_3d_mapped) != VK_SUCCESS) {
            LOG_FATAL("Failed to map 3D instance buffer");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }
    }

    /* Light UBO (std140 layout, 80 bytes, persistently mapped) */
    {
        VkDeviceSize ubo_size = 80;
        res = vk_create_buffer(&r->vk, ubo_size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->vk.light_ubo, &r->vk.light_ubo_memory);
        if (res != ENGINE_SUCCESS) goto fail;

        if (vkMapMemory(r->vk.device, r->vk.light_ubo_memory,
                        0, ubo_size, 0, &r->vk.light_ubo_mapped) != VK_SUCCESS) {
            LOG_FATAL("Failed to map light UBO");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Write default light */
        struct {
            f32 direction[4]; /* xyz + pad */
            f32 color[4];
            f32 ambient[4];
            f32 view_pos[4];
            f32 shininess[4];
        } default_light = {
            .direction = { 0.0f, -1.0f, 0.0f, 0.0f },
            .color     = { 1.0f,  1.0f, 1.0f, 0.0f },
            .ambient   = { 0.1f,  0.1f, 0.1f, 0.0f },
            .view_pos  = { 0.0f,  0.0f, 0.0f, 0.0f },
            .shininess = { 32.0f, 0.0f, 0.0f, 0.0f },
        };
        memcpy(r->vk.light_ubo_mapped, &default_light, sizeof(default_light));

        /* Light descriptor pool (1 UBO) */
        VkDescriptorPoolSize pool_size = {
            .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
        };
        VkDescriptorPoolCreateInfo pool_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = 1,
            .poolSizeCount = 1,
            .pPoolSizes    = &pool_size,
        };
        if (vkCreateDescriptorPool(r->vk.device, &pool_info, NULL,
                                    &r->vk.light_desc_pool) != VK_SUCCESS) {
            LOG_FATAL("Failed to create light descriptor pool");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Allocate light descriptor set */
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = r->vk.light_desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &r->vk.light_desc_set_layout,
        };
        if (vkAllocateDescriptorSets(r->vk.device, &alloc_info,
                                      &r->vk.light_desc_set) != VK_SUCCESS) {
            LOG_FATAL("Failed to allocate light descriptor set");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Write UBO to descriptor set */
        VkDescriptorBufferInfo buf_info = {
            .buffer = r->vk.light_ubo,
            .offset = 0,
            .range  = ubo_size,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = r->vk.light_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo     = &buf_info,
        };
        vkUpdateDescriptorSets(r->vk.device, 1, &write, 0, NULL);
    }

    /* ---- Skinned 3D pipeline and buffers ---- */
    if ((res = vk_create_skinned_3d_pipeline(&r->vk)) != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_vertex_buffer_skinned(&r->vk, MAX_SKINNED_VERTICES_3D)) != ENGINE_SUCCESS) goto fail;

    /* Skinned instance buffer (CPU-visible, persistently mapped, reuses InstanceData3D) */
    {
        VkDeviceSize buf_size = sizeof(InstanceData3D) * MAX_SKINNED_DRAW_COMMANDS;
        r->vk.instance_skinned_capacity = MAX_SKINNED_DRAW_COMMANDS;
        r->vk.instance_skinned_count = 0;

        res = vk_create_buffer(&r->vk, buf_size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->vk.instance_buffer_skinned, &r->vk.instance_buffer_skinned_memory);
        if (res != ENGINE_SUCCESS) goto fail;

        if (vkMapMemory(r->vk.device, r->vk.instance_buffer_skinned_memory,
                        0, buf_size, 0, &r->vk.instance_skinned_mapped) != VK_SUCCESS) {
            LOG_FATAL("Failed to map skinned instance buffer");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }
    }

    /* Joint matrix SSBO (CPU-visible, persistently mapped) */
    {
        /* 128 joints * 64 bytes * 64 draws = ~512 KB */
        u32 ssbo_capacity = MAX_JOINTS * sizeof(f32) * 16 * MAX_SKINNED_DRAW_COMMANDS;
        r->vk.joint_ssbo_capacity = ssbo_capacity;
        r->vk.joint_ssbo_used_bytes = 0;

        res = vk_create_buffer(&r->vk, ssbo_capacity,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &r->vk.joint_ssbo, &r->vk.joint_ssbo_memory);
        if (res != ENGINE_SUCCESS) goto fail;

        if (vkMapMemory(r->vk.device, r->vk.joint_ssbo_memory,
                        0, ssbo_capacity, 0, &r->vk.joint_ssbo_mapped) != VK_SUCCESS) {
            LOG_FATAL("Failed to map joint SSBO");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Joint SSBO descriptor pool (1 SSBO) */
        VkDescriptorPoolSize pool_size = {
            .type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
        };
        VkDescriptorPoolCreateInfo pool_info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = 1,
            .poolSizeCount = 1,
            .pPoolSizes    = &pool_size,
        };
        if (vkCreateDescriptorPool(r->vk.device, &pool_info, NULL,
                                    &r->vk.joint_desc_pool) != VK_SUCCESS) {
            LOG_FATAL("Failed to create joint descriptor pool");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Allocate joint descriptor set */
        VkDescriptorSetAllocateInfo alloc_info = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = r->vk.joint_desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &r->vk.joint_desc_set_layout,
        };
        if (vkAllocateDescriptorSets(r->vk.device, &alloc_info,
                                      &r->vk.joint_desc_set) != VK_SUCCESS) {
            LOG_FATAL("Failed to allocate joint descriptor set");
            res = ENGINE_ERROR_VULKAN_INIT;
            goto fail;
        }

        /* Write SSBO to descriptor set */
        VkDescriptorBufferInfo buf_info = {
            .buffer = r->vk.joint_ssbo,
            .offset = 0,
            .range  = ssbo_capacity,
        };
        VkWriteDescriptorSet write = {
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = r->vk.joint_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .pBufferInfo     = &buf_info,
        };
        vkUpdateDescriptorSets(r->vk.device, 1, &write, 0, NULL);
    }

    /* Bloom post-processing (creates all bloom resources — disabled by default) */
    if ((res = bloom_init(&r->vk)) != ENGINE_SUCCESS) goto fail;

    if ((res = vk_create_command_buffers(&r->vk))    != ENGINE_SUCCESS) goto fail;
    if ((res = vk_create_sync_objects(&r->vk))       != ENGINE_SUCCESS) goto fail;

    LOG_INFO("Renderer initialized successfully");
    *out_renderer = r;
    return ENGINE_SUCCESS;

fail:
    free(r);
    return res;
}

void renderer_destroy(Renderer *renderer) {
    if (renderer) {
        VulkanContext *vk = &renderer->vk;

        vkDeviceWaitIdle(vk->device);

        /* Bloom cleanup */
        bloom_shutdown(vk);

        /* 2D Instance buffer cleanup */
        if (vk->instance_mapped) {
            vkUnmapMemory(vk->device, vk->instance_buffer_memory);
            vk->instance_mapped = NULL;
        }
        if (vk->instance_buffer) {
            vkDestroyBuffer(vk->device, vk->instance_buffer, NULL);
            vkFreeMemory(vk->device, vk->instance_buffer_memory, NULL);
        }

        /* 3D cleanup */
        if (vk->instance_3d_mapped) {
            vkUnmapMemory(vk->device, vk->instance_buffer_3d_memory);
            vk->instance_3d_mapped = NULL;
        }
        if (vk->instance_buffer_3d) {
            vkDestroyBuffer(vk->device, vk->instance_buffer_3d, NULL);
            vkFreeMemory(vk->device, vk->instance_buffer_3d_memory, NULL);
        }
        if (vk->vertex_buffer_3d) {
            vkDestroyBuffer(vk->device, vk->vertex_buffer_3d, NULL);
            vkFreeMemory(vk->device, vk->vertex_buffer_3d_memory, NULL);
        }
        if (vk->index_buffer) {
            vkDestroyBuffer(vk->device, vk->index_buffer, NULL);
            vkFreeMemory(vk->device, vk->index_buffer_memory, NULL);
        }
        if (vk->light_ubo_mapped) {
            vkUnmapMemory(vk->device, vk->light_ubo_memory);
            vk->light_ubo_mapped = NULL;
        }
        if (vk->light_ubo) {
            vkDestroyBuffer(vk->device, vk->light_ubo, NULL);
            vkFreeMemory(vk->device, vk->light_ubo_memory, NULL);
        }
        if (vk->light_desc_pool)
            vkDestroyDescriptorPool(vk->device, vk->light_desc_pool, NULL);
        if (vk->light_desc_set_layout)
            vkDestroyDescriptorSetLayout(vk->device, vk->light_desc_set_layout, NULL);
        if (vk->graphics_pipeline_3d)
            vkDestroyPipeline(vk->device, vk->graphics_pipeline_3d, NULL);
        if (vk->pipeline_layout_3d)
            vkDestroyPipelineLayout(vk->device, vk->pipeline_layout_3d, NULL);

        /* Skinned 3D cleanup */
        if (vk->instance_skinned_mapped) {
            vkUnmapMemory(vk->device, vk->instance_buffer_skinned_memory);
            vk->instance_skinned_mapped = NULL;
        }
        if (vk->instance_buffer_skinned) {
            vkDestroyBuffer(vk->device, vk->instance_buffer_skinned, NULL);
            vkFreeMemory(vk->device, vk->instance_buffer_skinned_memory, NULL);
        }
        if (vk->joint_ssbo_mapped) {
            vkUnmapMemory(vk->device, vk->joint_ssbo_memory);
            vk->joint_ssbo_mapped = NULL;
        }
        if (vk->joint_ssbo) {
            vkDestroyBuffer(vk->device, vk->joint_ssbo, NULL);
            vkFreeMemory(vk->device, vk->joint_ssbo_memory, NULL);
        }
        if (vk->joint_desc_pool)
            vkDestroyDescriptorPool(vk->device, vk->joint_desc_pool, NULL);
        if (vk->vertex_buffer_skinned) {
            vkDestroyBuffer(vk->device, vk->vertex_buffer_skinned, NULL);
            vkFreeMemory(vk->device, vk->vertex_buffer_skinned_memory, NULL);
        }
        if (vk->graphics_pipeline_skinned)
            vkDestroyPipeline(vk->device, vk->graphics_pipeline_skinned, NULL);
        if (vk->pipeline_layout_skinned)
            vkDestroyPipelineLayout(vk->device, vk->pipeline_layout_skinned, NULL);
        if (vk->joint_desc_set_layout)
            vkDestroyDescriptorSetLayout(vk->device, vk->joint_desc_set_layout, NULL);

        /* Texture cleanup */
        for (u32 i = 0; i < vk->texture_count; i++) {
            vk_destroy_texture(vk, &vk->textures[i]);
        }
        vk_destroy_texture(vk, &vk->dummy_texture);

        text_shutdown(vk);
        vk_destroy(vk);
        free(renderer);
        LOG_INFO("Renderer destroyed");
    }
}

EngineResult renderer_begin_frame(Renderer *renderer) {
    VulkanContext *vk = &renderer->vk;
    u32 frame = vk->current_frame;

    /* Reset per-frame state */
    vk->instance_count              = 0;
    vk->draw_command_count          = 0;
    vk->instance_3d_count           = 0;
    vk->draw_command_3d_count       = 0;
    vk->instance_skinned_count      = 0;
    vk->draw_command_skinned_count  = 0;
    vk->joint_ssbo_used_bytes       = 0;

    /* Default camera: centered at origin, no rotation, zoom 1 */
    Camera2D default_cam = { .position = {0.0f, 0.0f}, .rotation = 0.0f, .zoom = 1.0f };
    compute_vp_matrix(vk, &default_cam);

    /* Wait for previous frame to finish */
    vkWaitForFences(vk->device, 1, &vk->in_flight[frame], VK_TRUE, UINT64_MAX);

    /* Acquire next swapchain image */
    VkResult result = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX,
                                             vk->image_available[frame], VK_NULL_HANDLE,
                                             &renderer->current_image_index);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        EngineResult res = recreate_swapchain(renderer);
        if (res != ENGINE_SUCCESS) return res;
        /* Re-acquire after recreation */
        result = vkAcquireNextImageKHR(vk->device, vk->swapchain, UINT64_MAX,
                                       vk->image_available[frame], VK_NULL_HANDLE,
                                       &renderer->current_image_index);
        if (result != VK_SUCCESS) {
            LOG_ERROR("Failed to acquire swapchain image after recreation");
            return ENGINE_ERROR_VULKAN_SWAPCHAIN;
        }
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("Failed to acquire swapchain image");
        return ENGINE_ERROR_VULKAN_SWAPCHAIN;
    }

    vkResetFences(vk->device, 1, &vk->in_flight[frame]);
    return ENGINE_SUCCESS;
}

EngineResult renderer_end_frame(Renderer *renderer) {
    VulkanContext *vk = &renderer->vk;
    u32 frame = vk->current_frame;

    /* Upload instance data (already in persistently mapped buffer via draw_mesh) */

    /* Record command buffer */
    vkResetCommandBuffer(vk->command_buffers[frame], 0);
    EngineResult res = record_command_buffer(renderer, vk->command_buffers[frame],
                                              renderer->current_image_index);
    if (res != ENGINE_SUCCESS) return res;

    /* Submit */
    VkSemaphore wait_sems[]   = { vk->image_available[frame] };
    VkSemaphore signal_sems[] = { vk->render_finished[frame] };
    VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = wait_sems,
        .pWaitDstStageMask    = wait_stages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &vk->command_buffers[frame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = signal_sems,
    };

    if (vkQueueSubmit(vk->graphics_queue, 1, &submit_info, vk->in_flight[frame]) != VK_SUCCESS) {
        LOG_ERROR("Failed to submit draw command buffer");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Present */
    VkSwapchainKHR swapchains[] = { vk->swapchain };
    VkPresentInfoKHR present_info = {
        .sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = signal_sems,
        .swapchainCount     = 1,
        .pSwapchains        = swapchains,
        .pImageIndices      = &renderer->current_image_index,
    };

    VkResult result = vkQueuePresentKHR(vk->present_queue, &present_info);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreate_swapchain(renderer);
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("Failed to present swapchain image");
        return ENGINE_ERROR_VULKAN_SWAPCHAIN;
    }

    vk->current_frame = (frame + 1) % MAX_FRAMES_IN_FLIGHT;
    return ENGINE_SUCCESS;
}

void renderer_set_clear_color(Renderer *renderer, f32 r, f32 g, f32 b, f32 a) {
    renderer->vk.clear_color[0] = r;
    renderer->vk.clear_color[1] = g;
    renderer->vk.clear_color[2] = b;
    renderer->vk.clear_color[3] = a;
}

void renderer_set_camera(Renderer *renderer, const Camera2D *camera) {
    compute_vp_matrix(&renderer->vk, camera);
}

EngineResult renderer_upload_mesh(Renderer *renderer,
                                  const Vertex *vertices, u32 count,
                                  MeshHandle *out_handle) {
    return vk_upload_mesh(&renderer->vk, vertices, count, out_handle);
}

void renderer_draw_mesh(Renderer *renderer, MeshHandle mesh,
                        const InstanceData *instances, u32 instance_count) {
    VulkanContext *vk = &renderer->vk;

    if (instance_count == 0) return;
    if (mesh >= vk->mesh_count) {
        LOG_WARN("Invalid mesh handle %u (have %u meshes)", mesh, vk->mesh_count);
        return;
    }
    if (vk->draw_command_count >= MAX_DRAW_COMMANDS) {
        LOG_WARN("Draw command list full (%u)", MAX_DRAW_COMMANDS);
        return;
    }

    /* Append instances to the per-frame instance buffer */
    u32 remaining = vk->instance_capacity - vk->instance_count;
    if (instance_count > remaining) {
        LOG_WARN("Instance buffer full (%u/%u), clamping",
                 vk->instance_count + instance_count, vk->instance_capacity);
        instance_count = remaining;
        if (instance_count == 0) return;
    }

    u32 inst_offset = vk->instance_count;

    InstanceData *dst = (InstanceData *)vk->instance_mapped + vk->instance_count;
    memcpy(dst, instances, sizeof(InstanceData) * instance_count);
    vk->instance_count += instance_count;

    /* Record draw command */
    DrawCommand *dc = &vk->draw_commands[vk->draw_command_count++];
    dc->mesh            = mesh;
    dc->texture         = TEXTURE_HANDLE_INVALID;
    dc->instance_offset = inst_offset;
    dc->instance_count  = instance_count;
}

void renderer_draw_mesh_textured(Renderer *renderer, MeshHandle mesh,
                                  TextureHandle texture,
                                  const InstanceData *instances,
                                  u32 instance_count) {
    VulkanContext *vk = &renderer->vk;

    if (instance_count == 0) return;
    if (mesh >= vk->mesh_count) {
        LOG_WARN("Invalid mesh handle %u (have %u meshes)", mesh, vk->mesh_count);
        return;
    }
    if (texture >= vk->texture_count) {
        LOG_WARN("Invalid texture handle %u (have %u textures)", texture, vk->texture_count);
        return;
    }
    if (vk->draw_command_count >= MAX_DRAW_COMMANDS) {
        LOG_WARN("Draw command list full (%u)", MAX_DRAW_COMMANDS);
        return;
    }

    /* Append instances to the per-frame instance buffer */
    u32 remaining = vk->instance_capacity - vk->instance_count;
    if (instance_count > remaining) {
        LOG_WARN("Instance buffer full (%u/%u), clamping",
                 vk->instance_count + instance_count, vk->instance_capacity);
        instance_count = remaining;
        if (instance_count == 0) return;
    }

    u32 inst_offset = vk->instance_count;

    InstanceData *dst = (InstanceData *)vk->instance_mapped + vk->instance_count;
    memcpy(dst, instances, sizeof(InstanceData) * instance_count);
    vk->instance_count += instance_count;

    /* Record draw command */
    DrawCommand *dc = &vk->draw_commands[vk->draw_command_count++];
    dc->mesh            = mesh;
    dc->texture         = texture;
    dc->instance_offset = inst_offset;
    dc->instance_count  = instance_count;
}

EngineResult renderer_load_texture(Renderer *renderer, const char *path,
                                    TextureFilter filter,
                                    TextureHandle *out_handle) {
    VulkanContext *vk = &renderer->vk;

    if (vk->texture_count >= MAX_TEXTURES) {
        LOG_ERROR("Texture table full (%u/%u)", vk->texture_count, MAX_TEXTURES);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Map public enum to Vulkan filter */
    VkFilter vk_filter = (filter == TEXTURE_FILTER_PIXELART)
        ? VK_FILTER_NEAREST
        : VK_FILTER_LINEAR;

    /* Decode image file with stb_image */
    int width, height, channels;
    stbi_uc *pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);
    if (!pixels) {
        LOG_ERROR("Failed to load texture: %s", path);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Upload to GPU */
    TextureHandle handle = (TextureHandle)vk->texture_count;
    EngineResult res = vk_create_texture(vk, pixels, (u32)width, (u32)height,
                                          VK_FORMAT_R8G8B8A8_SRGB, vk_filter,
                                          &vk->textures[handle]);
    stbi_image_free(pixels);

    if (res != ENGINE_SUCCESS) return res;

    /* Allocate descriptor set for this texture */
    VkDescriptorSetAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = vk->geo_desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &vk->geo_desc_set_layout,
    };

    if (vkAllocateDescriptorSets(vk->device, &alloc_info,
                                  &vk->texture_desc_sets[handle]) != VK_SUCCESS) {
        LOG_ERROR("Failed to allocate descriptor set for texture %u", handle);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Write texture to descriptor set */
    VkDescriptorImageInfo image_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = vk->textures[handle].view,
        .sampler     = vk->textures[handle].sampler,
    };

    VkWriteDescriptorSet write = {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = vk->texture_desc_sets[handle],
        .dstBinding      = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .pImageInfo      = &image_info,
    };

    vkUpdateDescriptorSets(vk->device, 1, &write, 0, NULL);

    vk->texture_count++;
    *out_handle = handle;

    LOG_INFO("Texture %u loaded: \"%s\" (%dx%d)", handle, path, width, height);
    return ENGINE_SUCCESS;
}

void renderer_draw_text(Renderer *renderer, const char *str,
                        f32 x, f32 y, f32 scale,
                        f32 r, f32 g, f32 b) {
    text_draw(&renderer->vk, str, x, y, scale, r, g, b);
}

void renderer_get_extent(const Renderer *renderer, u32 *width, u32 *height) {
    *width  = renderer->vk.swapchain_extent.width;
    *height = renderer->vk.swapchain_extent.height;
}

EngineResult renderer_handle_resize(Renderer *renderer) {
    return recreate_swapchain(renderer);
}

void renderer_set_bloom(Renderer *renderer, bool enabled, f32 intensity, f32 threshold) {
    renderer->vk.bloom.enabled = enabled;
    renderer->bloom_settings.intensity = intensity;
    renderer->bloom_settings.threshold = threshold;
}

void renderer_set_bloom_settings(Renderer *renderer, const BloomSettings *settings) {
    renderer->bloom_settings = *settings;
}

/* ---- 3D Rendering API ---- */

void renderer_set_camera_3d(Renderer *renderer, const Camera3D *camera) {
    compute_vp_matrix_3d(&renderer->vk, camera);
}

void renderer_set_light(Renderer *renderer, const DirectionalLight *light) {
    VulkanContext *vk = &renderer->vk;

    /* std140 layout: each vec3 padded to vec4 (16 bytes) */
    struct {
        f32 direction[4];
        f32 color[4];
        f32 ambient[4];
        f32 view_pos[4];
        f32 shininess[4];
    } ubo_data = {0};

    memcpy(ubo_data.direction, light->direction, sizeof(f32) * 3);
    memcpy(ubo_data.color, light->color, sizeof(f32) * 3);
    memcpy(ubo_data.ambient, light->ambient, sizeof(f32) * 3);
    memcpy(ubo_data.view_pos, vk->view_position, sizeof(f32) * 3);
    ubo_data.shininess[0] = light->shininess;

    memcpy(vk->light_ubo_mapped, &ubo_data, sizeof(ubo_data));
}

EngineResult renderer_upload_mesh_3d(Renderer *renderer,
                                     const Vertex3D *vertices, u32 vertex_count,
                                     const u32 *indices, u32 index_count,
                                     MeshHandle *out_handle) {
    return vk_upload_mesh_3d(&renderer->vk, vertices, vertex_count,
                              indices, index_count, out_handle);
}

void renderer_draw_mesh_3d(Renderer *renderer, MeshHandle mesh,
                           const InstanceData3D *instances, u32 instance_count) {
    VulkanContext *vk = &renderer->vk;

    if (instance_count == 0) return;
    if (mesh >= vk->mesh_count) {
        LOG_WARN("Invalid mesh handle %u (have %u meshes)", mesh, vk->mesh_count);
        return;
    }
    if (!vk->meshes[mesh].is_3d) {
        LOG_WARN("Mesh %u is not a 3D mesh — use renderer_draw_mesh instead", mesh);
        return;
    }
    if (vk->draw_command_3d_count >= MAX_DRAW_COMMANDS) {
        LOG_WARN("3D draw command list full (%u)", MAX_DRAW_COMMANDS);
        return;
    }

    u32 remaining = vk->instance_3d_capacity - vk->instance_3d_count;
    if (instance_count > remaining) {
        LOG_WARN("3D instance buffer full (%u/%u), clamping",
                 vk->instance_3d_count + instance_count, vk->instance_3d_capacity);
        instance_count = remaining;
        if (instance_count == 0) return;
    }

    u32 inst_offset = vk->instance_3d_count;

    InstanceData3D *dst = (InstanceData3D *)vk->instance_3d_mapped + vk->instance_3d_count;
    memcpy(dst, instances, sizeof(InstanceData3D) * instance_count);
    vk->instance_3d_count += instance_count;

    DrawCommand *dc = &vk->draw_commands_3d[vk->draw_command_3d_count++];
    dc->mesh            = mesh;
    dc->texture         = TEXTURE_HANDLE_INVALID;
    dc->instance_offset = inst_offset;
    dc->instance_count  = instance_count;
}

void renderer_draw_mesh_3d_textured(Renderer *renderer, MeshHandle mesh,
                                    TextureHandle texture,
                                    const InstanceData3D *instances,
                                    u32 instance_count) {
    VulkanContext *vk = &renderer->vk;

    if (instance_count == 0) return;
    if (mesh >= vk->mesh_count) {
        LOG_WARN("Invalid mesh handle %u (have %u meshes)", mesh, vk->mesh_count);
        return;
    }
    if (!vk->meshes[mesh].is_3d) {
        LOG_WARN("Mesh %u is not a 3D mesh — use renderer_draw_mesh_textured instead", mesh);
        return;
    }
    if (texture >= vk->texture_count) {
        LOG_WARN("Invalid texture handle %u (have %u textures)", texture, vk->texture_count);
        return;
    }
    if (vk->draw_command_3d_count >= MAX_DRAW_COMMANDS) {
        LOG_WARN("3D draw command list full (%u)", MAX_DRAW_COMMANDS);
        return;
    }

    u32 remaining = vk->instance_3d_capacity - vk->instance_3d_count;
    if (instance_count > remaining) {
        LOG_WARN("3D instance buffer full (%u/%u), clamping",
                 vk->instance_3d_count + instance_count, vk->instance_3d_capacity);
        instance_count = remaining;
        if (instance_count == 0) return;
    }

    u32 inst_offset = vk->instance_3d_count;

    InstanceData3D *dst = (InstanceData3D *)vk->instance_3d_mapped + vk->instance_3d_count;
    memcpy(dst, instances, sizeof(InstanceData3D) * instance_count);
    vk->instance_3d_count += instance_count;

    DrawCommand *dc = &vk->draw_commands_3d[vk->draw_command_3d_count++];
    dc->mesh            = mesh;
    dc->texture         = texture;
    dc->instance_offset = inst_offset;
    dc->instance_count  = instance_count;
}

/* ---- Skeletal Animation API ---- */

EngineResult renderer_load_skinned_model_file(Renderer *renderer, const char *path,
                                              SkinnedModel *out_model) {
    return renderer_load_skinned_model(&renderer->vk, path, out_model);
}

EngineResult renderer_upload_mesh_skinned(Renderer *renderer,
                                          const SkinnedVertex3D *vertices, u32 vertex_count,
                                          const u32 *indices, u32 index_count,
                                          MeshHandle *out_handle) {
    return vk_upload_mesh_skinned(&renderer->vk, vertices, vertex_count,
                                   indices, index_count, out_handle);
}

/* Internal helper for skinned draw (shared by textured + untextured) */
static void draw_skinned_internal(Renderer *renderer, MeshHandle mesh,
                                   TextureHandle texture,
                                   const InstanceData3D *instance,
                                   const f32 joint_matrices[][16], u32 joint_count) {
    VulkanContext *vk = &renderer->vk;

    if (mesh >= vk->mesh_count) {
        LOG_WARN("Invalid mesh handle %u (have %u meshes)", mesh, vk->mesh_count);
        return;
    }
    if (!vk->meshes[mesh].is_skinned) {
        LOG_WARN("Mesh %u is not a skinned mesh", mesh);
        return;
    }
    if (vk->draw_command_skinned_count >= MAX_SKINNED_DRAW_COMMANDS) {
        LOG_WARN("Skinned draw command list full (%u)", MAX_SKINNED_DRAW_COMMANDS);
        return;
    }
    if (joint_count == 0 || !joint_matrices) {
        LOG_WARN("No joint matrices provided for skinned draw");
        return;
    }

    /* Copy instance data (1 instance per skinned draw) */
    if (vk->instance_skinned_count >= vk->instance_skinned_capacity) {
        LOG_WARN("Skinned instance buffer full");
        return;
    }
    u32 inst_offset = vk->instance_skinned_count;
    InstanceData3D *dst = (InstanceData3D *)vk->instance_skinned_mapped + inst_offset;
    memcpy(dst, instance, sizeof(InstanceData3D));
    vk->instance_skinned_count++;

    /* Copy joint matrices to SSBO (256-byte aligned for buffer dynamic offset compat) */
    u32 joint_data_size = joint_count * sizeof(f32) * 16; /* joint_count * 64 bytes */
    u32 aligned_offset = (vk->joint_ssbo_used_bytes + 255) & ~255u; /* 256-byte align */

    if (aligned_offset + joint_data_size > vk->joint_ssbo_capacity) {
        LOG_WARN("Joint SSBO full (%u + %u > %u)",
                 aligned_offset, joint_data_size, vk->joint_ssbo_capacity);
        vk->instance_skinned_count--; /* rollback */
        return;
    }

    u8 *ssbo_dst = (u8 *)vk->joint_ssbo_mapped + aligned_offset;
    memcpy(ssbo_dst, joint_matrices, joint_data_size);
    vk->joint_ssbo_used_bytes = aligned_offset + joint_data_size;

    /* Record draw command */
    SkinnedDrawCommand *dc = &vk->draw_commands_skinned[vk->draw_command_skinned_count++];
    dc->mesh              = mesh;
    dc->texture           = texture;
    dc->instance_offset   = inst_offset;
    dc->instance_count    = 1;
    dc->joint_ssbo_offset = aligned_offset;
    dc->joint_count       = joint_count;
}

void renderer_draw_skinned(Renderer *renderer, MeshHandle mesh,
                           const InstanceData3D *instance,
                           const f32 joint_matrices[][16], u32 joint_count) {
    draw_skinned_internal(renderer, mesh, TEXTURE_HANDLE_INVALID,
                          instance, joint_matrices, joint_count);
}

void renderer_draw_skinned_textured(Renderer *renderer, MeshHandle mesh,
                                    TextureHandle texture,
                                    const InstanceData3D *instance,
                                    const f32 joint_matrices[][16], u32 joint_count) {
    draw_skinned_internal(renderer, mesh, texture,
                          instance, joint_matrices, joint_count);
}
