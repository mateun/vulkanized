#include "renderer/vk_pipeline.h"
#include "core/log.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

/* --------------------------------------------------------------------------
 * Shader module loading
 * ------------------------------------------------------------------------ */

u8 *vk_read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        LOG_ERROR("Failed to open file: %s", path);
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    u8 *buf = malloc((size_t)size);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    fread(buf, 1, (size_t)size, f);
    fclose(f);

    *out_size = (size_t)size;
    return buf;
}

VkShaderModule vk_create_shader_module(VkDevice device, const u8 *code, size_t size) {
    VkShaderModuleCreateInfo create_info = {
        .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size,
        .pCode    = (const u32 *)code,
    };

    VkShaderModule module;
    if (vkCreateShaderModule(device, &create_info, NULL, &module) != VK_SUCCESS) {
        LOG_ERROR("Failed to create shader module");
        return VK_NULL_HANDLE;
    }
    return module;
}

/* --------------------------------------------------------------------------
 * Render pass
 * ------------------------------------------------------------------------ */

EngineResult vk_create_render_pass(VulkanContext *ctx) {
    VkAttachmentDescription color_attachment = {
        .format         = ctx->swapchain_format,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    };

    VkAttachmentDescription depth_attachment = {
        .format         = VK_FORMAT_D32_SFLOAT,
        .samples        = VK_SAMPLE_COUNT_1_BIT,
        .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentDescription attachments[] = { color_attachment, depth_attachment };

    VkAttachmentReference color_ref = {
        .attachment = 0,
        .layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkAttachmentReference depth_ref = {
        .attachment = 1,
        .layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpass = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = 1,
        .pColorAttachments       = &color_ref,
        .pDepthStencilAttachment = &depth_ref,
    };

    VkSubpassDependency dependency = {
        .srcSubpass    = VK_SUBPASS_EXTERNAL,
        .dstSubpass    = 0,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
    };

    VkRenderPassCreateInfo rp_info = {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 2,
        .pAttachments    = attachments,
        .subpassCount    = 1,
        .pSubpasses      = &subpass,
        .dependencyCount = 1,
        .pDependencies   = &dependency,
    };

    if (vkCreateRenderPass(ctx->device, &rp_info, NULL, &ctx->render_pass) != VK_SUCCESS) {
        LOG_FATAL("Failed to create render pass");
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    LOG_DEBUG("Render pass created (color + depth)");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Graphics pipeline
 * ------------------------------------------------------------------------ */

EngineResult vk_create_graphics_pipeline(VulkanContext *ctx) {
    /* Load SPIR-V shaders */
    size_t vert_size, frag_size;
    u8 *vert_code = vk_read_file("shaders/triangle.vert.spv", &vert_size);
    u8 *frag_code = vk_read_file("shaders/triangle.frag.spv", &frag_size);

    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        LOG_FATAL("Failed to load shader files");
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    VkShaderModule vert_module = vk_create_shader_module(ctx->device, vert_code, vert_size);
    VkShaderModule frag_module = vk_create_shader_module(ctx->device, frag_code, frag_size);
    free(vert_code);
    free(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName  = "main",
        },
    };

    /* Vertex input: binding 0 = per-vertex, binding 1 = per-instance */
    VkVertexInputBindingDescription bindings[] = {
        { /* Per-vertex data */
            .binding   = 0,
            .stride    = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        },
        { /* Per-instance data */
            .binding   = 1,
            .stride    = sizeof(InstanceData),
            .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE,
        },
    };

    VkVertexInputAttributeDescription attributes[] = {
        { /* position: location 0, vec2 (per-vertex) */
            .binding  = 0,
            .location = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(Vertex, position),
        },
        { /* uv: location 1, vec2 (per-vertex) */
            .binding  = 0,
            .location = 1,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(Vertex, uv),
        },
        { /* color: location 2, vec3 (per-vertex) */
            .binding  = 0,
            .location = 2,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(Vertex, color),
        },
        { /* inst_position: location 3, vec2 (per-instance) */
            .binding  = 1,
            .location = 3,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(InstanceData, position),
        },
        { /* inst_rotation: location 4, float (per-instance) */
            .binding  = 1,
            .location = 4,
            .format   = VK_FORMAT_R32_SFLOAT,
            .offset   = offsetof(InstanceData, rotation),
        },
        { /* inst_scale: location 5, vec2 (per-instance) */
            .binding  = 1,
            .location = 5,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(InstanceData, scale),
        },
        { /* inst_color: location 6, vec3 (per-instance) */
            .binding  = 1,
            .location = 6,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(InstanceData, color),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = ENGINE_ARRAY_LEN(bindings),
        .pVertexBindingDescriptions      = bindings,
        .vertexAttributeDescriptionCount = ENGINE_ARRAY_LEN(attributes),
        .pVertexAttributeDescriptions    = attributes,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    /* Dynamic viewport and scissor */
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ENGINE_ARRAY_LEN(dynamic_states),
        .pDynamicStates    = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable     = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };

    /* Descriptor set layout: one combined image sampler for geometry textures */
    VkDescriptorSetLayoutBinding geo_sampler_binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo geo_desc_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &geo_sampler_binding,
    };

    if (vkCreateDescriptorSetLayout(ctx->device, &geo_desc_layout_info, NULL,
                                     &ctx->geo_desc_set_layout) != VK_SUCCESS) {
        LOG_FATAL("Failed to create geometry descriptor set layout");
        vkDestroyShaderModule(ctx->device, vert_module, NULL);
        vkDestroyShaderModule(ctx->device, frag_module, NULL);
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    /* Descriptor pool for geometry textures */
    VkDescriptorPoolSize pool_size = {
        .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = MAX_TEXTURES,
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = MAX_TEXTURES,
        .poolSizeCount = 1,
        .pPoolSizes    = &pool_size,
    };

    if (vkCreateDescriptorPool(ctx->device, &pool_info, NULL,
                                &ctx->geo_desc_pool) != VK_SUCCESS) {
        LOG_FATAL("Failed to create geometry descriptor pool");
        vkDestroyShaderModule(ctx->device, vert_module, NULL);
        vkDestroyShaderModule(ctx->device, frag_module, NULL);
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    /* Pipeline layout: VP matrix + use_texture flag as push constants (68 bytes),
     * plus one descriptor set for texture sampling */
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset     = 0,
        .size       = 68, /* mat4 (64) + uint use_texture (4) */
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &ctx->geo_desc_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_range,
    };

    if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                               &ctx->pipeline_layout) != VK_SUCCESS) {
        LOG_FATAL("Failed to create pipeline layout");
        vkDestroyShaderModule(ctx->device, vert_module, NULL);
        vkDestroyShaderModule(ctx->device, frag_module, NULL);
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    /* Graphics pipeline */
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = ENGINE_ARRAY_LEN(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState  = &rasterizer,
        .pMultisampleState    = &multisampling,
        .pDepthStencilState   = &depth_stencil,
        .pColorBlendState     = &color_blending,
        .pDynamicState        = &dynamic_state,
        .layout               = ctx->pipeline_layout,
        .renderPass           = ctx->render_pass,
        .subpass              = 0,
    };

    VkResult result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                                 &pipeline_info, NULL,
                                                 &ctx->graphics_pipeline);

    vkDestroyShaderModule(ctx->device, vert_module, NULL);
    vkDestroyShaderModule(ctx->device, frag_module, NULL);

    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create graphics pipeline");
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    LOG_INFO("Graphics pipeline created");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Text pipeline (alpha-blended, textured quads with push-constant ortho)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_text_pipeline(VulkanContext *ctx) {
    /* Load text shaders */
    size_t vert_size, frag_size;
    u8 *vert_code = vk_read_file("shaders/text.vert.spv", &vert_size);
    u8 *frag_code = vk_read_file("shaders/text.frag.spv", &frag_size);

    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        LOG_FATAL("Failed to load text shader files");
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    VkShaderModule vert_module = vk_create_shader_module(ctx->device, vert_code, vert_size);
    VkShaderModule frag_module = vk_create_shader_module(ctx->device, frag_code, frag_size);
    free(vert_code);
    free(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName  = "main",
        },
    };

    /* TextVertex: position(vec2) + uv(vec2) + color(vec3) */
    VkVertexInputBindingDescription binding = {
        .binding   = 0,
        .stride    = sizeof(TextVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        {
            .binding  = 0,
            .location = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(TextVertex, position),
        },
        {
            .binding  = 0,
            .location = 1,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = offsetof(TextVertex, uv),
        },
        {
            .binding  = 0,
            .location = 2,
            .format   = VK_FORMAT_R32G32B32_SFLOAT,
            .offset   = offsetof(TextVertex, color),
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &binding,
        .vertexAttributeDescriptionCount = ENGINE_ARRAY_LEN(attributes),
        .pVertexAttributeDescriptions    = attributes,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ENGINE_ARRAY_LEN(dynamic_states),
        .pDynamicStates    = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_NONE, /* No culling for 2D text quads */
        .frontFace   = VK_FRONT_FACE_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    /* Text is a 2D overlay — no depth testing */
    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    /* Alpha blending for text */
    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };

    /* Descriptor set layout: one combined image sampler for font atlas */
    VkDescriptorSetLayoutBinding sampler_binding = {
        .binding         = 0,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
    };

    VkDescriptorSetLayoutCreateInfo desc_layout_info = {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = &sampler_binding,
    };

    if (vkCreateDescriptorSetLayout(ctx->device, &desc_layout_info, NULL,
                                     &ctx->text_desc_set_layout) != VK_SUCCESS) {
        LOG_FATAL("Failed to create text descriptor set layout");
        vkDestroyShaderModule(ctx->device, vert_module, NULL);
        vkDestroyShaderModule(ctx->device, frag_module, NULL);
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    /* Push constant: screen_size (vec2 = 8 bytes) */
    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset     = 0,
        .size       = sizeof(f32) * 2,
    };

    VkPipelineLayoutCreateInfo layout_info = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1,
        .pSetLayouts            = &ctx->text_desc_set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges    = &push_range,
    };

    if (vkCreatePipelineLayout(ctx->device, &layout_info, NULL,
                                &ctx->text_pipeline_layout) != VK_SUCCESS) {
        LOG_FATAL("Failed to create text pipeline layout");
        vkDestroyShaderModule(ctx->device, vert_module, NULL);
        vkDestroyShaderModule(ctx->device, frag_module, NULL);
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = ENGINE_ARRAY_LEN(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState  = &rasterizer,
        .pMultisampleState    = &multisampling,
        .pDepthStencilState   = &depth_stencil,
        .pColorBlendState     = &color_blending,
        .pDynamicState        = &dynamic_state,
        .layout               = ctx->text_pipeline_layout,
        .renderPass           = ctx->render_pass,
        .subpass              = 0,
    };

    VkResult result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                                 &pipeline_info, NULL,
                                                 &ctx->text_pipeline);

    vkDestroyShaderModule(ctx->device, vert_module, NULL);
    vkDestroyShaderModule(ctx->device, frag_module, NULL);

    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create text graphics pipeline");
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    LOG_INFO("Text pipeline created");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Bloom scene pipelines (geometry + text against HDR render pass)
 * Same shaders and config as the main pipelines, but attached to the
 * bloom scene render pass (R16G16B16A16_SFLOAT + depth).
 * ------------------------------------------------------------------------ */

EngineResult vk_create_bloom_scene_pipelines(VulkanContext *ctx) {
    /* ---- Geometry pipeline (same as vk_create_graphics_pipeline but for bloom render pass) ---- */
    size_t vert_size, frag_size;
    u8 *vert_code = vk_read_file("shaders/triangle.vert.spv", &vert_size);
    u8 *frag_code = vk_read_file("shaders/triangle.frag.spv", &frag_size);

    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        LOG_FATAL("Failed to load shader files for bloom scene pipeline");
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    VkShaderModule vert_module = vk_create_shader_module(ctx->device, vert_code, vert_size);
    VkShaderModule frag_module = vk_create_shader_module(ctx->device, frag_code, frag_size);
    free(vert_code);
    free(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    VkPipelineShaderStageCreateInfo shader_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName  = "main",
        },
    };

    VkVertexInputBindingDescription bindings[] = {
        { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
        { .binding = 1, .stride = sizeof(InstanceData), .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE },
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, position) },
        { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, uv) },
        { .binding = 0, .location = 2, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, color) },
        { .binding = 1, .location = 3, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(InstanceData, position) },
        { .binding = 1, .location = 4, .format = VK_FORMAT_R32_SFLOAT, .offset = offsetof(InstanceData, rotation) },
        { .binding = 1, .location = 5, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(InstanceData, scale) },
        { .binding = 1, .location = 6, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(InstanceData, color) },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = ENGINE_ARRAY_LEN(bindings),
        .pVertexBindingDescriptions      = bindings,
        .vertexAttributeDescriptionCount = ENGINE_ARRAY_LEN(attributes),
        .pVertexAttributeDescriptions    = attributes,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = ENGINE_ARRAY_LEN(dynamic_states),
        .pDynamicStates    = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount  = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_BACK_BIT,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL,
    };

    VkPipelineColorBlendAttachmentState blend_attachment = {
        .colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable         = VK_TRUE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &blend_attachment,
    };

    /* Reuse the existing pipeline layout (same push constants + descriptors) */
    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = ENGINE_ARRAY_LEN(shader_stages),
        .pStages             = shader_stages,
        .pVertexInputState   = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState  = &rasterizer,
        .pMultisampleState    = &multisampling,
        .pDepthStencilState   = &depth_stencil,
        .pColorBlendState     = &color_blending,
        .pDynamicState        = &dynamic_state,
        .layout               = ctx->pipeline_layout,       /* same layout as main geo pipeline */
        .renderPass           = ctx->bloom.scene_render_pass, /* HDR render pass */
        .subpass              = 0,
    };

    VkResult result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                                 &pipeline_info, NULL,
                                                 &ctx->bloom.scene_graphics_pipeline);

    vkDestroyShaderModule(ctx->device, vert_module, NULL);
    vkDestroyShaderModule(ctx->device, frag_module, NULL);

    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create bloom scene graphics pipeline");
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    /* ---- Text pipeline for bloom scene ---- */
    vert_code = vk_read_file("shaders/text.vert.spv", &vert_size);
    frag_code = vk_read_file("shaders/text.frag.spv", &frag_size);

    if (!vert_code || !frag_code) {
        free(vert_code);
        free(frag_code);
        LOG_FATAL("Failed to load text shaders for bloom scene pipeline");
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    vert_module = vk_create_shader_module(ctx->device, vert_code, vert_size);
    frag_module = vk_create_shader_module(ctx->device, frag_code, frag_size);
    free(vert_code);
    free(frag_code);

    if (vert_module == VK_NULL_HANDLE || frag_module == VK_NULL_HANDLE) {
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    VkPipelineShaderStageCreateInfo text_stages[] = {
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_module,
            .pName  = "main",
        },
        {
            .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_module,
            .pName  = "main",
        },
    };

    VkVertexInputBindingDescription text_binding = {
        .binding   = 0,
        .stride    = sizeof(TextVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription text_attribs[] = {
        { .binding = 0, .location = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(TextVertex, position) },
        { .binding = 0, .location = 1, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(TextVertex, uv) },
        { .binding = 0, .location = 2, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(TextVertex, color) },
    };

    VkPipelineVertexInputStateCreateInfo text_vertex_input = {
        .sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount   = 1,
        .pVertexBindingDescriptions      = &text_binding,
        .vertexAttributeDescriptionCount = ENGINE_ARRAY_LEN(text_attribs),
        .pVertexAttributeDescriptions    = text_attribs,
    };

    VkPipelineRasterizationStateCreateInfo text_rasterizer = {
        .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth   = 1.0f,
        .cullMode    = VK_CULL_MODE_NONE,
        .frontFace   = VK_FRONT_FACE_CLOCKWISE,
    };

    VkPipelineDepthStencilStateCreateInfo text_depth = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkGraphicsPipelineCreateInfo text_pipe_info = {
        .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount          = ENGINE_ARRAY_LEN(text_stages),
        .pStages             = text_stages,
        .pVertexInputState   = &text_vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState      = &viewport_state,
        .pRasterizationState  = &text_rasterizer,
        .pMultisampleState    = &multisampling,
        .pDepthStencilState   = &text_depth,
        .pColorBlendState     = &color_blending,
        .pDynamicState        = &dynamic_state,
        .layout               = ctx->text_pipeline_layout,
        .renderPass           = ctx->bloom.scene_render_pass,
        .subpass              = 0,
    };

    result = vkCreateGraphicsPipelines(ctx->device, VK_NULL_HANDLE, 1,
                                        &text_pipe_info, NULL,
                                        &ctx->bloom.scene_text_pipeline);

    vkDestroyShaderModule(ctx->device, vert_module, NULL);
    vkDestroyShaderModule(ctx->device, frag_module, NULL);

    if (result != VK_SUCCESS) {
        LOG_FATAL("Failed to create bloom scene text pipeline");
        return ENGINE_ERROR_VULKAN_PIPELINE;
    }

    LOG_INFO("Bloom scene pipelines created (geometry + text)");
    return ENGINE_SUCCESS;
}
