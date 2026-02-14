#ifndef ENGINE_VK_TYPES_H
#define ENGINE_VK_TYPES_H

#include "renderer/renderer_types.h"
#include <vulkan/vulkan.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_MESHES           32
#define MAX_TEXTURES         64
#define MAX_DRAW_COMMANDS    256

/* ---- Texture handle ---- */

typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkSampler      sampler;
    u32            width;
    u32            height;
} VulkanTexture;

/* ---- Mesh slot (region within the shared vertex buffer) ---- */

typedef struct {
    u32 first_vertex;  /* offset into the shared vertex buffer */
    u32 vertex_count;  /* number of vertices in this mesh */
} MeshSlot;

/* ---- Per-frame draw command (queued by renderer_draw_mesh) ---- */

typedef struct {
    MeshHandle    mesh;            /* which mesh to draw */
    TextureHandle texture;         /* TEXTURE_HANDLE_INVALID = untextured */
    u32           instance_offset; /* offset into instance buffer */
    u32           instance_count;  /* number of instances */
} DrawCommand;

typedef struct {
    VkInstance               instance;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkPhysicalDevice         physical_device;
    VkDevice                 device;
    VkSurfaceKHR             surface;

    /* Queue handles */
    VkQueue                  graphics_queue;
    VkQueue                  present_queue;
    u32                      graphics_family;
    u32                      present_family;

    /* Swapchain */
    VkSwapchainKHR           swapchain;
    VkFormat                 swapchain_format;
    VkExtent2D               swapchain_extent;
    u32                      swapchain_image_count;
    VkImage                 *swapchain_images;
    VkImageView             *swapchain_image_views;

    /* Render pass & pipeline */
    VkRenderPass             render_pass;
    VkPipelineLayout         pipeline_layout;
    VkPipeline               graphics_pipeline;

    /* Depth buffer (single image shared across all swapchain images) */
    VkImage                  depth_image;
    VkDeviceMemory           depth_memory;
    VkImageView              depth_image_view;

    /* Framebuffers (one per swapchain image) */
    VkFramebuffer           *framebuffers;

    /* Shared vertex buffer (all meshes packed sequentially, GPU-local) */
    VkBuffer                 vertex_buffer;
    VkDeviceMemory           vertex_buffer_memory;
    u32                      vertex_total;       /* total vertices across all meshes */

    /* Mesh table */
    MeshSlot                 meshes[MAX_MESHES];
    u32                      mesh_count;

    /* Texture table (loaded textures) */
    VulkanTexture            textures[MAX_TEXTURES];
    VkDescriptorSet          texture_desc_sets[MAX_TEXTURES]; /* one desc set per texture */
    u32                      texture_count;

    /* Geometry descriptor set layout + pool (for texture sampling) */
    VkDescriptorSetLayout    geo_desc_set_layout;
    VkDescriptorPool         geo_desc_pool;

    /* 1x1 white dummy texture — bound for untextured draws to satisfy descriptor set 0 */
    VulkanTexture            dummy_texture;
    VkDescriptorSet          dummy_desc_set;

    /* Clear color (set by game, used in render pass begin) */
    float                    clear_color[4]; /* r, g, b, a */

    /* Camera VP matrix (pushed as push constant for geometry pipeline) */
    float                    vp_matrix[16]; /* mat4, column-major */

    /* Instance buffer (per-frame, CPU-visible, persistently mapped) */
    VkBuffer                 instance_buffer;
    VkDeviceMemory           instance_buffer_memory;
    void                    *instance_mapped;
    u32                      instance_count;     /* total instances queued this frame */
    u32                      instance_capacity;  /* max instances */

    /* Draw command list (filled by renderer_draw_mesh, consumed by record_command_buffer) */
    DrawCommand              draw_commands[MAX_DRAW_COMMANDS];
    u32                      draw_command_count;

    /* Text rendering */
    VkPipelineLayout         text_pipeline_layout;
    VkPipeline               text_pipeline;
    VkDescriptorSetLayout    text_desc_set_layout;
    VkDescriptorPool         text_desc_pool;
    VkDescriptorSet          text_desc_set;
    VulkanTexture            font_atlas;
    VkBuffer                 text_vertex_buffer;
    VkDeviceMemory           text_vertex_buffer_memory;
    void                    *text_vertex_mapped;   /* persistently mapped pointer */
    u32                      text_vertex_count;
    u32                      text_vertex_capacity;

    /* Command pool & buffers */
    VkCommandPool            command_pool;
    VkCommandBuffer          command_buffers[MAX_FRAMES_IN_FLIGHT];

    /* Synchronization */
    VkSemaphore              image_available[MAX_FRAMES_IN_FLIGHT];
    VkSemaphore              render_finished[MAX_FRAMES_IN_FLIGHT];
    VkFence                  in_flight[MAX_FRAMES_IN_FLIGHT];

    u32                      current_frame;
} VulkanContext;

#endif /* ENGINE_VK_TYPES_H */
