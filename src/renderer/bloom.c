#include "renderer/vk_types.h"
#include "renderer/bloom.h"
#include "renderer/vk_pipeline.h"
#include "renderer/vk_buffer.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* HDR format for the offscreen scene image */
#define BLOOM_HDR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

/* Half-res format for the bloom ping-pong images */
#define BLOOM_BLUR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

/* --------------------------------------------------------------------------
 * Helper: find suitable memory type
 * ------------------------------------------------------------------------ */

static u32 find_memory_type(VulkanContext *vk, u32 type_filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(vk->physical_device, &mem_props);

    for (u32 i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((type_filter & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    LOG_FATAL("Bloom: failed to find suitable memory type");
    return UINT32_MAX;
}

/* --------------------------------------------------------------------------
 * Helper: create an image + memory + view + sampler
 * ------------------------------------------------------------------------ */

static EngineResult create_bloom_image(VulkanContext *vk, u32 width, u32 height,
                                        VkFormat format, VkImageUsageFlags usage,
                                        VkImage *out_image, VkDeviceMemory *out_memory,
                                        VkImageView *out_view, VkSampler *out_sampler) {
    VkImageCreateInfo img_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent        = { width, height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .format        = format,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = usage,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
    };

    if (vkCreateImage(vk->device, &img_info, NULL, out_image) != VK_SUCCESS) {
        LOG_FATAL("Bloom: failed to create image (%ux%u)", width, height);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk->device, *out_image, &mem_reqs);

    u32 mem_type = find_memory_type(vk, mem_reqs.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(vk->device, *out_image, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryAllocateInfo alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(vk->device, &alloc, NULL, out_memory) != VK_SUCCESS) {
        vkDestroyImage(vk->device, *out_image, NULL);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    vkBindImageMemory(vk->device, *out_image, *out_memory, 0);

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = *out_image,
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

    if (vkCreateImageView(vk->device, &view_info, NULL, out_view) != VK_SUCCESS) {
        vkFreeMemory(vk->device, *out_memory, NULL);
        vkDestroyImage(vk->device, *out_image, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkSamplerCreateInfo sampler_info = {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_LINEAR,
        .minFilter    = VK_FILTER_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    };

    if (vkCreateSampler(vk->device, &sampler_info, NULL, out_sampler) != VK_SUCCESS) {
        vkDestroyImageView(vk->device, *out_view, NULL);
        vkFreeMemory(vk->device, *out_memory, NULL);
        vkDestroyImage(vk->device, *out_image, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    return ENGINE_SUCCESS;
}

static void destroy_bloom_image(VulkanContext *vk, VkImage *image, VkDeviceMemory *memory,
                                 VkImageView *view, VkSampler *sampler) {
    if (*sampler)  { vkDestroySampler(vk->device, *sampler, NULL);    *sampler = VK_NULL_HANDLE; }
    if (*view)     { vkDestroyImageView(vk->device, *view, NULL);     *view = VK_NULL_HANDLE; }
    if (*image)    { vkDestroyImage(vk->device, *image, NULL);        *image = VK_NULL_HANDLE; }
    if (*memory)   { vkFreeMemory(vk->device, *memory, NULL);         *memory = VK_NULL_HANDLE; }
}

/* --------------------------------------------------------------------------
 * Helper: create depth buffer for bloom scene
 * ------------------------------------------------------------------------ */

static EngineResult create_bloom_depth(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    VkImageCreateInfo img_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent        = { vk->swapchain_extent.width, vk->swapchain_extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .format        = VK_FORMAT_D32_SFLOAT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
    };

    if (vkCreateImage(vk->device, &img_info, NULL, &b->depth_image) != VK_SUCCESS)
        return ENGINE_ERROR_VULKAN_INIT;

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(vk->device, b->depth_image, &mem_reqs);
    u32 mem_type = find_memory_type(vk, mem_reqs.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) return ENGINE_ERROR_VULKAN_INIT;

    VkMemoryAllocateInfo alloc = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(vk->device, &alloc, NULL, &b->depth_memory) != VK_SUCCESS)
        return ENGINE_ERROR_OUT_OF_MEMORY;

    vkBindImageMemory(vk->device, b->depth_image, b->depth_memory, 0);

    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = b->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(vk->device, &view_info, NULL, &b->depth_view) != VK_SUCCESS)
        return ENGINE_ERROR_VULKAN_INIT;

    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Render pass creation
 * ------------------------------------------------------------------------ */

static EngineResult create_render_passes(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* --- Pass 1: Scene render pass (HDR color + depth) --- */
    {
        VkAttachmentDescription attachments[] = {
            { /* HDR color */
                .format         = BLOOM_HDR_FORMAT,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            },
            { /* Depth */
                .format         = VK_FORMAT_D32_SFLOAT,
                .samples        = VK_SAMPLE_COUNT_1_BIT,
                .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        };

        VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkAttachmentReference depth_ref = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {
            .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount    = 1,
            .pColorAttachments       = &color_ref,
            .pDepthStencilAttachment = &depth_ref,
        };

        VkSubpassDependency dep = {
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
            .pDependencies   = &dep,
        };

        if (vkCreateRenderPass(vk->device, &rp_info, NULL, &b->scene_render_pass) != VK_SUCCESS) {
            LOG_FATAL("Bloom: failed to create scene render pass");
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* --- Post-process render pass (single HDR color, shared by extract + blur) --- */
    {
        VkAttachmentDescription attachment = {
            .format         = BLOOM_BLUR_FORMAT,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };

        VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_ref,
        };

        VkSubpassDependency dep = {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo rp_info = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &attachment,
            .subpassCount    = 1,
            .pSubpasses      = &subpass,
            .dependencyCount = 1,
            .pDependencies   = &dep,
        };

        if (vkCreateRenderPass(vk->device, &rp_info, NULL, &b->postprocess_render_pass) != VK_SUCCESS) {
            LOG_FATAL("Bloom: failed to create postprocess render pass");
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* --- Composite render pass (single swapchain color) --- */
    {
        VkAttachmentDescription attachment = {
            .format         = vk->swapchain_format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        };

        VkAttachmentReference color_ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

        VkSubpassDescription subpass = {
            .pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments    = &color_ref,
        };

        VkSubpassDependency dep = {
            .srcSubpass    = VK_SUBPASS_EXTERNAL,
            .dstSubpass    = 0,
            .srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        VkRenderPassCreateInfo rp_info = {
            .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments    = &attachment,
            .subpassCount    = 1,
            .pSubpasses      = &subpass,
            .dependencyCount = 1,
            .pDependencies   = &dep,
        };

        if (vkCreateRenderPass(vk->device, &rp_info, NULL, &b->composite_render_pass) != VK_SUCCESS) {
            LOG_FATAL("Bloom: failed to create composite render pass");
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    LOG_DEBUG("Bloom render passes created");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Descriptor layouts, pool, and sets
 * ------------------------------------------------------------------------ */

static EngineResult create_descriptors(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* Single sampler layout (extract, blur) */
    {
        VkDescriptorSetLayoutBinding binding = {
            .binding         = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
        };

        VkDescriptorSetLayoutCreateInfo info = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings    = &binding,
        };

        if (vkCreateDescriptorSetLayout(vk->device, &info, NULL,
                                         &b->single_sampler_layout) != VK_SUCCESS) {
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* Dual sampler layout (composite: scene + bloom) */
    {
        VkDescriptorSetLayoutBinding bindings[] = {
            {
                .binding         = 0,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
            {
                .binding         = 1,
                .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };

        VkDescriptorSetLayoutCreateInfo info = {
            .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 2,
            .pBindings    = bindings,
        };

        if (vkCreateDescriptorSetLayout(vk->device, &info, NULL,
                                         &b->dual_sampler_layout) != VK_SUCCESS) {
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* Descriptor pool: 3 single-sampler sets + 1 dual-sampler set = 5 samplers total, 4 sets */
    {
        VkDescriptorPoolSize pool_size = {
            .type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 5,
        };

        VkDescriptorPoolCreateInfo info = {
            .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets       = 4,
            .poolSizeCount = 1,
            .pPoolSizes    = &pool_size,
        };

        if (vkCreateDescriptorPool(vk->device, &info, NULL, &b->desc_pool) != VK_SUCCESS) {
            return ENGINE_ERROR_VULKAN_INIT;
        }
    }

    /* Allocate descriptor sets */
    {
        VkDescriptorSetLayout single_layouts[] = {
            b->single_sampler_layout,
            b->single_sampler_layout,
            b->single_sampler_layout,
        };

        VkDescriptorSetAllocateInfo alloc = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = b->desc_pool,
            .descriptorSetCount = 3,
            .pSetLayouts        = single_layouts,
        };

        VkDescriptorSet sets[3];
        if (vkAllocateDescriptorSets(vk->device, &alloc, sets) != VK_SUCCESS) {
            return ENGINE_ERROR_VULKAN_INIT;
        }

        b->extract_desc_set = sets[0];
        b->blur_h_desc_set  = sets[1];
        b->blur_v_desc_set  = sets[2];
    }

    {
        VkDescriptorSetAllocateInfo alloc = {
            .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool     = b->desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts        = &b->dual_sampler_layout,
        };

        if (vkAllocateDescriptorSets(vk->device, &alloc, &b->composite_desc_set) != VK_SUCCESS) {
            return ENGINE_ERROR_VULKAN_INIT;
        }
    }

    LOG_DEBUG("Bloom descriptors created");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Write/update descriptor sets to point at current images
 * ------------------------------------------------------------------------ */

static void update_descriptor_sets(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* extract_desc_set: binding 0 = scene_image */
    VkDescriptorImageInfo scene_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = b->scene_view,
        .sampler     = b->scene_sampler,
    };

    /* blur_h_desc_set: binding 0 = bloom_a */
    VkDescriptorImageInfo bloom_a_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = b->bloom_a_view,
        .sampler     = b->bloom_a_sampler,
    };

    /* blur_v_desc_set: binding 0 = bloom_b */
    VkDescriptorImageInfo bloom_b_info = {
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .imageView   = b->bloom_b_view,
        .sampler     = b->bloom_b_sampler,
    };

    VkWriteDescriptorSet writes[] = {
        { /* extract: scene */
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = b->extract_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &scene_info,
        },
        { /* blur_h: bloom_a */
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = b->blur_h_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &bloom_a_info,
        },
        { /* blur_v: bloom_b */
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = b->blur_v_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &bloom_b_info,
        },
        { /* composite binding 0: scene */
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = b->composite_desc_set,
            .dstBinding      = 0,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &scene_info,
        },
        { /* composite binding 1: bloom_a */
            .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet          = b->composite_desc_set,
            .dstBinding      = 1,
            .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .pImageInfo      = &bloom_a_info,
        },
    };

    vkUpdateDescriptorSets(vk->device, 5, writes, 0, NULL);
}

/* --------------------------------------------------------------------------
 * Pipeline creation (extract, blur, composite)
 * ------------------------------------------------------------------------ */

static EngineResult create_postprocess_pipelines(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* Shared fullscreen vertex shader */
    size_t vert_size;
    u8 *vert_code = vk_read_file("shaders/fullscreen.vert.spv", &vert_size);
    if (!vert_code) {
        LOG_FATAL("Bloom: failed to load fullscreen.vert.spv");
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }
    VkShaderModule vert_module = vk_create_shader_module(vk->device, vert_code, vert_size);
    free(vert_code);
    if (vert_module == VK_NULL_HANDLE) return ENGINE_ERROR_VULKAN_PIPELINE;

    /* Shared pipeline state for fullscreen passes (no vertex input, no depth, no blending) */
    VkPipelineVertexInputStateCreateInfo empty_vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };

    VkDynamicState dynamic_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
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
        .cullMode    = VK_CULL_MODE_NONE,
        .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    VkPipelineDepthStencilStateCreateInfo no_depth = {
        .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable  = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState no_blend = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments    = &no_blend,
    };

    /* ---- Extract pipeline ---- */
    {
        size_t frag_size;
        u8 *frag_code = vk_read_file("shaders/bloom_extract.frag.spv", &frag_size);
        if (!frag_code) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_FILE_NOT_FOUND;
        }
        VkShaderModule frag_module = vk_create_shader_module(vk->device, frag_code, frag_size);
        free(frag_code);
        if (frag_module == VK_NULL_HANDLE) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkPipelineShaderStageCreateInfo stages[] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
        };

        /* Push constants: threshold(f32) + soft_threshold(f32) = 8 bytes */
        VkPushConstantRange push = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 8,
        };

        VkPipelineLayoutCreateInfo layout_info = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = 1,
            .pSetLayouts            = &b->single_sampler_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        };

        if (vkCreatePipelineLayout(vk->device, &layout_info, NULL, &b->extract_layout) != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, frag_module, NULL);
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkGraphicsPipelineCreateInfo pipe_info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &empty_vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState      = &viewport_state,
            .pRasterizationState  = &rasterizer,
            .pMultisampleState    = &multisampling,
            .pDepthStencilState   = &no_depth,
            .pColorBlendState     = &color_blending,
            .pDynamicState        = &dynamic_state,
            .layout               = b->extract_layout,
            .renderPass           = b->postprocess_render_pass,
            .subpass              = 0,
        };

        VkResult result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1,
                                                     &pipe_info, NULL, &b->extract_pipeline);
        vkDestroyShaderModule(vk->device, frag_module, NULL);

        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* ---- Blur pipeline ---- */
    {
        size_t frag_size;
        u8 *frag_code = vk_read_file("shaders/bloom_blur.frag.spv", &frag_size);
        if (!frag_code) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_FILE_NOT_FOUND;
        }
        VkShaderModule frag_module = vk_create_shader_module(vk->device, frag_code, frag_size);
        free(frag_code);
        if (frag_module == VK_NULL_HANDLE) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkPipelineShaderStageCreateInfo stages[] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
        };

        /* Push constants: direction(vec2) = 8 bytes */
        VkPushConstantRange push = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 8,
        };

        VkPipelineLayoutCreateInfo layout_info = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = 1,
            .pSetLayouts            = &b->single_sampler_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        };

        if (vkCreatePipelineLayout(vk->device, &layout_info, NULL, &b->blur_layout) != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, frag_module, NULL);
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkGraphicsPipelineCreateInfo pipe_info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &empty_vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState      = &viewport_state,
            .pRasterizationState  = &rasterizer,
            .pMultisampleState    = &multisampling,
            .pDepthStencilState   = &no_depth,
            .pColorBlendState     = &color_blending,
            .pDynamicState        = &dynamic_state,
            .layout               = b->blur_layout,
            .renderPass           = b->postprocess_render_pass,
            .subpass              = 0,
        };

        VkResult result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1,
                                                     &pipe_info, NULL, &b->blur_pipeline);
        vkDestroyShaderModule(vk->device, frag_module, NULL);

        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    /* ---- Composite pipeline ---- */
    {
        size_t frag_size;
        u8 *frag_code = vk_read_file("shaders/bloom_composite.frag.spv", &frag_size);
        if (!frag_code) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_FILE_NOT_FOUND;
        }
        VkShaderModule frag_module = vk_create_shader_module(vk->device, frag_code, frag_size);
        free(frag_code);
        if (frag_module == VK_NULL_HANDLE) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkPipelineShaderStageCreateInfo stages[] = {
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vert_module, .pName = "main" },
            { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
              .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag_module, .pName = "main" },
        };

        /* Push constants: intensity + scanline_strength + scanline_count + aberration + screen_size = 24 bytes */
        VkPushConstantRange push = {
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size       = 24,
        };

        VkPipelineLayoutCreateInfo layout_info = {
            .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount         = 1,
            .pSetLayouts            = &b->dual_sampler_layout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges    = &push,
        };

        if (vkCreatePipelineLayout(vk->device, &layout_info, NULL, &b->composite_layout) != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, frag_module, NULL);
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }

        VkGraphicsPipelineCreateInfo pipe_info = {
            .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .stageCount          = 2,
            .pStages             = stages,
            .pVertexInputState   = &empty_vertex_input,
            .pInputAssemblyState = &input_assembly,
            .pViewportState      = &viewport_state,
            .pRasterizationState  = &rasterizer,
            .pMultisampleState    = &multisampling,
            .pDepthStencilState   = &no_depth,
            .pColorBlendState     = &color_blending,
            .pDynamicState        = &dynamic_state,
            .layout               = b->composite_layout,
            .renderPass           = b->composite_render_pass,
            .subpass              = 0,
        };

        VkResult result = vkCreateGraphicsPipelines(vk->device, VK_NULL_HANDLE, 1,
                                                     &pipe_info, NULL, &b->composite_pipeline);
        vkDestroyShaderModule(vk->device, frag_module, NULL);

        if (result != VK_SUCCESS) {
            vkDestroyShaderModule(vk->device, vert_module, NULL);
            return ENGINE_ERROR_VULKAN_PIPELINE;
        }
    }

    vkDestroyShaderModule(vk->device, vert_module, NULL);

    LOG_DEBUG("Bloom post-processing pipelines created");
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Size-dependent resource creation (images, framebuffers)
 * ------------------------------------------------------------------------ */

static EngineResult create_size_dependent_resources(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;
    EngineResult res;

    u32 w = vk->swapchain_extent.width;
    u32 h = vk->swapchain_extent.height;
    b->bloom_extent.width  = ENGINE_MAX(w / 2, 1);
    b->bloom_extent.height = ENGINE_MAX(h / 2, 1);

    /* Scene image (full-res HDR) */
    res = create_bloom_image(vk, w, h, BLOOM_HDR_FORMAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &b->scene_image, &b->scene_memory,
                              &b->scene_view, &b->scene_sampler);
    if (res != ENGINE_SUCCESS) return res;

    /* Bloom A (half-res) */
    res = create_bloom_image(vk, b->bloom_extent.width, b->bloom_extent.height,
                              BLOOM_BLUR_FORMAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &b->bloom_a_image, &b->bloom_a_memory,
                              &b->bloom_a_view, &b->bloom_a_sampler);
    if (res != ENGINE_SUCCESS) return res;

    /* Bloom B (half-res) */
    res = create_bloom_image(vk, b->bloom_extent.width, b->bloom_extent.height,
                              BLOOM_BLUR_FORMAT,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              &b->bloom_b_image, &b->bloom_b_memory,
                              &b->bloom_b_view, &b->bloom_b_sampler);
    if (res != ENGINE_SUCCESS) return res;

    /* Bloom depth buffer */
    res = create_bloom_depth(vk);
    if (res != ENGINE_SUCCESS) return res;

    /* --- Framebuffers --- */

    /* Scene framebuffer (HDR color + depth) */
    {
        VkImageView attachments[] = { b->scene_view, b->depth_view };
        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = b->scene_render_pass,
            .attachmentCount = 2,
            .pAttachments    = attachments,
            .width           = w,
            .height          = h,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(vk->device, &fb_info, NULL, &b->scene_framebuffer) != VK_SUCCESS)
            return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Extract framebuffer (-> bloom_a, half-res) */
    {
        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = b->postprocess_render_pass,
            .attachmentCount = 1,
            .pAttachments    = &b->bloom_a_view,
            .width           = b->bloom_extent.width,
            .height          = b->bloom_extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(vk->device, &fb_info, NULL, &b->extract_framebuffer) != VK_SUCCESS)
            return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Blur H framebuffer (-> bloom_b, half-res) */
    {
        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = b->postprocess_render_pass,
            .attachmentCount = 1,
            .pAttachments    = &b->bloom_b_view,
            .width           = b->bloom_extent.width,
            .height          = b->bloom_extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(vk->device, &fb_info, NULL, &b->blur_h_framebuffer) != VK_SUCCESS)
            return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Blur V framebuffer (-> bloom_a, half-res) */
    {
        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = b->postprocess_render_pass,
            .attachmentCount = 1,
            .pAttachments    = &b->bloom_a_view,
            .width           = b->bloom_extent.width,
            .height          = b->bloom_extent.height,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(vk->device, &fb_info, NULL, &b->blur_v_framebuffer) != VK_SUCCESS)
            return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Composite framebuffers (one per swapchain image) */
    b->composite_framebuffers = malloc(sizeof(VkFramebuffer) * vk->swapchain_image_count);
    for (u32 i = 0; i < vk->swapchain_image_count; i++) {
        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = b->composite_render_pass,
            .attachmentCount = 1,
            .pAttachments    = &vk->swapchain_image_views[i],
            .width           = w,
            .height          = h,
            .layers          = 1,
        };
        if (vkCreateFramebuffer(vk->device, &fb_info, NULL, &b->composite_framebuffers[i]) != VK_SUCCESS)
            return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Update descriptor sets with new image views */
    update_descriptor_sets(vk);

    LOG_DEBUG("Bloom size-dependent resources created (%ux%u, bloom %ux%u)",
              w, h, b->bloom_extent.width, b->bloom_extent.height);
    return ENGINE_SUCCESS;
}

static void destroy_size_dependent_resources(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* Composite framebuffers */
    if (b->composite_framebuffers) {
        for (u32 i = 0; i < vk->swapchain_image_count; i++) {
            vkDestroyFramebuffer(vk->device, b->composite_framebuffers[i], NULL);
        }
        free(b->composite_framebuffers);
        b->composite_framebuffers = NULL;
    }

    if (b->blur_v_framebuffer)  { vkDestroyFramebuffer(vk->device, b->blur_v_framebuffer, NULL);  b->blur_v_framebuffer  = VK_NULL_HANDLE; }
    if (b->blur_h_framebuffer)  { vkDestroyFramebuffer(vk->device, b->blur_h_framebuffer, NULL);  b->blur_h_framebuffer  = VK_NULL_HANDLE; }
    if (b->extract_framebuffer) { vkDestroyFramebuffer(vk->device, b->extract_framebuffer, NULL); b->extract_framebuffer = VK_NULL_HANDLE; }
    if (b->scene_framebuffer)   { vkDestroyFramebuffer(vk->device, b->scene_framebuffer, NULL);   b->scene_framebuffer   = VK_NULL_HANDLE; }

    /* Bloom depth */
    if (b->depth_view)   { vkDestroyImageView(vk->device, b->depth_view, NULL);  b->depth_view = VK_NULL_HANDLE; }
    if (b->depth_image)  { vkDestroyImage(vk->device, b->depth_image, NULL);     b->depth_image = VK_NULL_HANDLE; }
    if (b->depth_memory) { vkFreeMemory(vk->device, b->depth_memory, NULL);      b->depth_memory = VK_NULL_HANDLE; }

    /* Images */
    destroy_bloom_image(vk, &b->bloom_b_image, &b->bloom_b_memory, &b->bloom_b_view, &b->bloom_b_sampler);
    destroy_bloom_image(vk, &b->bloom_a_image, &b->bloom_a_memory, &b->bloom_a_view, &b->bloom_a_sampler);
    destroy_bloom_image(vk, &b->scene_image, &b->scene_memory, &b->scene_view, &b->scene_sampler);
}

/* --------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------ */

EngineResult bloom_init(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;
    memset(b, 0, sizeof(BloomContext));
    b->enabled = false;

    EngineResult res;

    if ((res = create_render_passes(vk))            != ENGINE_SUCCESS) return res;
    if ((res = create_descriptors(vk))              != ENGINE_SUCCESS) return res;
    if ((res = create_postprocess_pipelines(vk))    != ENGINE_SUCCESS) return res;
    if ((res = vk_create_bloom_scene_pipelines(vk)) != ENGINE_SUCCESS) return res;
    if ((res = create_size_dependent_resources(vk)) != ENGINE_SUCCESS) return res;

    LOG_INFO("Bloom post-processing initialized");
    return ENGINE_SUCCESS;
}

void bloom_shutdown(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    vkDeviceWaitIdle(vk->device);

    destroy_size_dependent_resources(vk);

    /* Pipelines */
    if (b->composite_pipeline)        vkDestroyPipeline(vk->device, b->composite_pipeline, NULL);
    if (b->composite_layout)          vkDestroyPipelineLayout(vk->device, b->composite_layout, NULL);
    if (b->blur_pipeline)             vkDestroyPipeline(vk->device, b->blur_pipeline, NULL);
    if (b->blur_layout)               vkDestroyPipelineLayout(vk->device, b->blur_layout, NULL);
    if (b->extract_pipeline)          vkDestroyPipeline(vk->device, b->extract_pipeline, NULL);
    if (b->extract_layout)            vkDestroyPipelineLayout(vk->device, b->extract_layout, NULL);
    if (b->scene_graphics_pipeline)   vkDestroyPipeline(vk->device, b->scene_graphics_pipeline, NULL);
    if (b->scene_text_pipeline)       vkDestroyPipeline(vk->device, b->scene_text_pipeline, NULL);

    /* Descriptors */
    if (b->desc_pool)                 vkDestroyDescriptorPool(vk->device, b->desc_pool, NULL);
    if (b->dual_sampler_layout)       vkDestroyDescriptorSetLayout(vk->device, b->dual_sampler_layout, NULL);
    if (b->single_sampler_layout)     vkDestroyDescriptorSetLayout(vk->device, b->single_sampler_layout, NULL);

    /* Render passes */
    if (b->composite_render_pass)     vkDestroyRenderPass(vk->device, b->composite_render_pass, NULL);
    if (b->postprocess_render_pass)   vkDestroyRenderPass(vk->device, b->postprocess_render_pass, NULL);
    if (b->scene_render_pass)         vkDestroyRenderPass(vk->device, b->scene_render_pass, NULL);

    memset(b, 0, sizeof(BloomContext));
    LOG_INFO("Bloom post-processing shut down");
}

EngineResult bloom_resize(VulkanContext *vk) {
    destroy_size_dependent_resources(vk);
    return create_size_dependent_resources(vk);
}

void bloom_cleanup_swapchain_deps(VulkanContext *vk) {
    BloomContext *b = &vk->bloom;

    /* Composite framebuffers reference swapchain image views —
     * must be destroyed before vk_cleanup_swapchain() destroys those views. */
    if (b->composite_framebuffers) {
        for (u32 i = 0; i < vk->swapchain_image_count; i++) {
            vkDestroyFramebuffer(vk->device, b->composite_framebuffers[i], NULL);
        }
        free(b->composite_framebuffers);
        b->composite_framebuffers = NULL;
    }
}

/* --------------------------------------------------------------------------
 * Record bloom passes 2-5
 * ------------------------------------------------------------------------ */

void bloom_record(VulkanContext *vk, VkCommandBuffer cmd,
                  const BloomSettings *settings, u32 image_index) {
    BloomContext *b = &vk->bloom;

    u32 bw = b->bloom_extent.width;
    u32 bh = b->bloom_extent.height;
    u32 fw = vk->swapchain_extent.width;
    u32 fh = vk->swapchain_extent.height;

    /* ---- Pass 2: Brightness extraction (scene -> bloom_a, half-res) ---- */
    {
        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = b->postprocess_render_pass,
            .framebuffer = b->extract_framebuffer,
            .renderArea  = { .offset = {0, 0}, .extent = b->bloom_extent },
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = { 0.0f, 0.0f, (float)bw, (float)bh, 0.0f, 1.0f };
        VkRect2D scissor = { .offset = {0, 0}, .extent = b->bloom_extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->extract_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 b->extract_layout, 0, 1, &b->extract_desc_set, 0, NULL);

        struct { float threshold; float soft_threshold; } extract_push = {
            settings->threshold, settings->soft_threshold
        };
        vkCmdPushConstants(cmd, b->extract_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 8, &extract_push);

        vkCmdDraw(cmd, 3, 1, 0, 0); /* fullscreen triangle */

        vkCmdEndRenderPass(cmd);
    }

    /* ---- Pass 3: Horizontal blur (bloom_a -> bloom_b, half-res) ---- */
    {
        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = b->postprocess_render_pass,
            .framebuffer = b->blur_h_framebuffer,
            .renderArea  = { .offset = {0, 0}, .extent = b->bloom_extent },
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = { 0.0f, 0.0f, (float)bw, (float)bh, 0.0f, 1.0f };
        VkRect2D scissor = { .offset = {0, 0}, .extent = b->bloom_extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->blur_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 b->blur_layout, 0, 1, &b->blur_h_desc_set, 0, NULL);

        float direction[2] = { 1.0f / (float)bw, 0.0f };
        vkCmdPushConstants(cmd, b->blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 8, direction);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    /* ---- Pass 4: Vertical blur (bloom_b -> bloom_a, half-res) ---- */
    {
        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = b->postprocess_render_pass,
            .framebuffer = b->blur_v_framebuffer,
            .renderArea  = { .offset = {0, 0}, .extent = b->bloom_extent },
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = { 0.0f, 0.0f, (float)bw, (float)bh, 0.0f, 1.0f };
        VkRect2D scissor = { .offset = {0, 0}, .extent = b->bloom_extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->blur_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 b->blur_layout, 0, 1, &b->blur_v_desc_set, 0, NULL);

        float direction[2] = { 0.0f, 1.0f / (float)bh };
        vkCmdPushConstants(cmd, b->blur_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 8, direction);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }

    /* ---- Pass 5: Composite (scene + bloom_a -> swapchain) ---- */
    {
        VkRenderPassBeginInfo rp_info = {
            .sType       = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass  = b->composite_render_pass,
            .framebuffer = b->composite_framebuffers[image_index],
            .renderArea  = { .offset = {0, 0}, .extent = vk->swapchain_extent },
        };

        vkCmdBeginRenderPass(cmd, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = { 0.0f, 0.0f, (float)fw, (float)fh, 0.0f, 1.0f };
        VkRect2D scissor = { .offset = {0, 0}, .extent = vk->swapchain_extent };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->composite_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 b->composite_layout, 0, 1, &b->composite_desc_set, 0, NULL);

        struct {
            float intensity;
            float scanline_strength;
            float scanline_count;
            float aberration;
            float screen_size[2];
        } composite_push = {
            settings->intensity,
            settings->scanline_strength,
            settings->scanline_count,
            settings->aberration,
            { (float)fw, (float)fh },
        };
        vkCmdPushConstants(cmd, b->composite_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, 24, &composite_push);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
    }
}
