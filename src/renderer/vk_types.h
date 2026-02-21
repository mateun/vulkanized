#ifndef ENGINE_VK_TYPES_H
#define ENGINE_VK_TYPES_H

#include "renderer/renderer_types.h"
#include <vulkan/vulkan.h>

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_MESHES           32
#define MAX_TEXTURES         64
#define MAX_DRAW_COMMANDS    256
#define MAX_VERTICES_3D      65536
#define MAX_INDICES          131072
#define MAX_SKINNED_VERTICES_3D  65536
#define MAX_SKINNED_DRAW_COMMANDS 64

/* ---- Texture handle ---- */

typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
    VkSampler      sampler;
    u32            width;
    u32            height;
} VulkanTexture;

/* ---- Mesh slot (region within a shared vertex buffer) ---- */

typedef struct {
    u32  first_vertex;  /* offset into the shared vertex buffer */
    u32  vertex_count;  /* number of vertices in this mesh */
    bool is_3d;         /* true = Vertex3D format, false = Vertex (2D) */
    bool is_skinned;    /* true = SkinnedVertex3D format (skeletal animation) */
    u32  first_index;   /* offset into the shared index buffer (3D only) */
    u32  index_count;   /* 0 = non-indexed draw */
} MeshSlot;

/* ---- Per-frame draw command (queued by renderer_draw_mesh) ---- */

typedef struct {
    MeshHandle    mesh;            /* which mesh to draw */
    TextureHandle texture;         /* TEXTURE_HANDLE_INVALID = untextured */
    u32           instance_offset; /* offset into instance buffer */
    u32           instance_count;  /* number of instances */
} DrawCommand;

/* ---- Per-frame skinned draw command (skeletal animation) ---- */

typedef struct {
    MeshHandle    mesh;
    TextureHandle texture;
    u32           instance_offset;
    u32           instance_count;
    u32           joint_ssbo_offset; /* byte offset into the joint SSBO */
    u32           joint_count;       /* number of joints for this draw */
} SkinnedDrawCommand;

/* ---- Bloom post-processing context ---- */

typedef struct {
    /* Offscreen images */
    VkImage        scene_image;
    VkDeviceMemory scene_memory;
    VkImageView    scene_view;
    VkSampler      scene_sampler;

    VkImage        bloom_a_image;
    VkDeviceMemory bloom_a_memory;
    VkImageView    bloom_a_view;
    VkSampler      bloom_a_sampler;

    VkImage        bloom_b_image;
    VkDeviceMemory bloom_b_memory;
    VkImageView    bloom_b_view;
    VkSampler      bloom_b_sampler;

    /* Render passes */
    VkRenderPass   scene_render_pass;       /* HDR color + depth */
    VkRenderPass   postprocess_render_pass; /* single HDR color (extract + blur) */
    VkRenderPass   composite_render_pass;   /* single swapchain-format color */

    /* Framebuffers */
    VkFramebuffer  scene_framebuffer;
    VkFramebuffer  extract_framebuffer;     /* -> bloom_a */
    VkFramebuffer  blur_h_framebuffer;      /* -> bloom_b */
    VkFramebuffer  blur_v_framebuffer;      /* -> bloom_a */
    VkFramebuffer *composite_framebuffers;  /* one per swapchain image */

    /* Scene pipelines (geometry + text, for HDR render pass) */
    VkPipeline     scene_graphics_pipeline;
    VkPipeline     scene_text_pipeline;
    VkPipeline     scene_3d_pipeline;        /* 3D geometry for HDR scene pass */
    VkPipeline     scene_skinned_pipeline;   /* Skinned 3D for HDR scene pass */

    /* Post-processing pipelines */
    VkPipelineLayout extract_layout;
    VkPipeline       extract_pipeline;
    VkPipelineLayout blur_layout;
    VkPipeline       blur_pipeline;
    VkPipelineLayout composite_layout;
    VkPipeline       composite_pipeline;

    /* Descriptors */
    VkDescriptorSetLayout single_sampler_layout; /* 1 combined image sampler */
    VkDescriptorSetLayout dual_sampler_layout;   /* 2 combined image samplers */
    VkDescriptorPool      desc_pool;
    VkDescriptorSet       extract_desc_set;      /* samples scene_image */
    VkDescriptorSet       blur_h_desc_set;       /* samples bloom_a */
    VkDescriptorSet       blur_v_desc_set;       /* samples bloom_b */
    VkDescriptorSet       composite_desc_set;    /* samples scene_image + bloom_a */

    /* Depth buffer for HDR scene rendering */
    VkImage        depth_image;
    VkDeviceMemory depth_memory;
    VkImageView    depth_view;

    VkExtent2D     bloom_extent; /* half-res */
    bool           enabled;
} BloomContext;

/* ---- Main Vulkan context ---- */

typedef struct VulkanContext {
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

    /* ---- 3D rendering ---- */

    /* 3D pipeline */
    VkPipelineLayout         pipeline_layout_3d;
    VkPipeline               graphics_pipeline_3d;

    /* 3D vertex buffer (separate from 2D, GPU-local) */
    VkBuffer                 vertex_buffer_3d;
    VkDeviceMemory           vertex_buffer_3d_memory;
    u32                      vertex_3d_total;

    /* Index buffer (shared, GPU-local, for 3D meshes) */
    VkBuffer                 index_buffer;
    VkDeviceMemory           index_buffer_memory;
    u32                      index_total;

    /* 3D instance buffer (CPU-visible, persistently mapped) */
    VkBuffer                 instance_buffer_3d;
    VkDeviceMemory           instance_buffer_3d_memory;
    void                    *instance_3d_mapped;
    u32                      instance_3d_count;
    u32                      instance_3d_capacity;

    /* Light UBO (single directional light) */
    VkBuffer                 light_ubo;
    VkDeviceMemory           light_ubo_memory;
    void                    *light_ubo_mapped;
    VkDescriptorSetLayout    light_desc_set_layout;
    VkDescriptorPool         light_desc_pool;
    VkDescriptorSet          light_desc_set;

    /* 3D draw commands */
    DrawCommand              draw_commands_3d[MAX_DRAW_COMMANDS];
    u32                      draw_command_3d_count;

    /* ---- Skinned 3D rendering (skeletal animation) ---- */

    /* Skinned pipeline */
    VkPipelineLayout         pipeline_layout_skinned;
    VkPipeline               graphics_pipeline_skinned;

    /* Skinned vertex buffer (GPU-local, separate from Vertex3D buffer) */
    VkBuffer                 vertex_buffer_skinned;
    VkDeviceMemory           vertex_buffer_skinned_memory;
    u32                      vertex_skinned_total;

    /* Skinned instance buffer (CPU-visible, reuses InstanceData3D layout) */
    VkBuffer                 instance_buffer_skinned;
    VkDeviceMemory           instance_buffer_skinned_memory;
    void                    *instance_skinned_mapped;
    u32                      instance_skinned_count;
    u32                      instance_skinned_capacity;

    /* Joint matrix SSBO (CPU-visible, persistently mapped) */
    VkBuffer                 joint_ssbo;
    VkDeviceMemory           joint_ssbo_memory;
    void                    *joint_ssbo_mapped;
    u32                      joint_ssbo_used_bytes;  /* bytes used this frame */
    u32                      joint_ssbo_capacity;    /* total bytes */

    /* Joint SSBO descriptor */
    VkDescriptorSetLayout    joint_desc_set_layout;
    VkDescriptorPool         joint_desc_pool;
    VkDescriptorSet          joint_desc_set;

    /* Skinned draw commands */
    SkinnedDrawCommand       draw_commands_skinned[MAX_SKINNED_DRAW_COMMANDS];
    u32                      draw_command_skinned_count;

    /* Cached camera position (for specular lighting) */
    float                    view_position[3];

    /* Bloom post-processing */
    BloomContext             bloom;

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
