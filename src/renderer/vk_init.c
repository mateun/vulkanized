#include "renderer/vk_init.h"
#include "platform/window.h"
#include "core/log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Validation layer support
 * ------------------------------------------------------------------------ */

#ifdef ENGINE_DEBUG
static const char *validation_layers[] = { "VK_LAYER_KHRONOS_validation" };
static const u32 validation_layer_count = 1;
#else
static const char **validation_layers = NULL;
static const u32 validation_layer_count = 0;
#endif

static const char *device_extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
static const u32 device_extension_count = 1;

/* --------------------------------------------------------------------------
 * Debug messenger callback
 * ------------------------------------------------------------------------ */

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *data,
    void *user_data)
{
    ENGINE_UNUSED(type);
    ENGINE_UNUSED(user_data);

    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_WARN("Vulkan validation: %s", data->pMessage);
    } else {
        LOG_TRACE("Vulkan validation: %s", data->pMessage);
    }
    return VK_FALSE;
}

/* --------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static bool check_validation_layer_support(void) {
    u32 count;
    vkEnumerateInstanceLayerProperties(&count, NULL);
    VkLayerProperties *available = malloc(sizeof(VkLayerProperties) * count);
    vkEnumerateInstanceLayerProperties(&count, available);

    for (u32 i = 0; i < validation_layer_count; i++) {
        bool found = false;
        for (u32 j = 0; j < count; j++) {
            if (strcmp(validation_layers[i], available[j].layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            free(available);
            return false;
        }
    }
    free(available);
    return true;
}

/* --------------------------------------------------------------------------
 * Queue family finding
 * ------------------------------------------------------------------------ */

typedef struct {
    u32  graphics;
    u32  present;
    bool has_graphics;
    bool has_present;
} QueueFamilyIndices;

static QueueFamilyIndices find_queue_families(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = {0};

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = malloc(sizeof(VkQueueFamilyProperties) * count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);

    for (u32 i = 0; i < count; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
            indices.has_graphics = true;
        }

        VkBool32 present_support = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present_support);
        if (present_support) {
            indices.present = i;
            indices.has_present = true;
        }

        if (indices.has_graphics && indices.has_present) break;
    }

    free(families);
    return indices;
}

/* --------------------------------------------------------------------------
 * Swapchain support query
 * ------------------------------------------------------------------------ */

typedef struct {
    VkSurfaceCapabilitiesKHR  capabilities;
    VkSurfaceFormatKHR       *formats;
    u32                       format_count;
    VkPresentModeKHR         *present_modes;
    u32                       present_mode_count;
} SwapchainSupport;

static SwapchainSupport query_swapchain_support(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupport support = {0};

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);

    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &support.format_count, NULL);
    if (support.format_count > 0) {
        support.formats = malloc(sizeof(VkSurfaceFormatKHR) * support.format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &support.format_count, support.formats);
    }

    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &support.present_mode_count, NULL);
    if (support.present_mode_count > 0) {
        support.present_modes = malloc(sizeof(VkPresentModeKHR) * support.present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &support.present_mode_count, support.present_modes);
    }

    return support;
}

static void free_swapchain_support(SwapchainSupport *support) {
    free(support->formats);
    free(support->present_modes);
    memset(support, 0, sizeof(*support));
}

static VkSurfaceFormatKHR choose_surface_format(const SwapchainSupport *support) {
    for (u32 i = 0; i < support->format_count; i++) {
        if (support->formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            support->formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return support->formats[i];
        }
    }
    return support->formats[0];
}

static VkPresentModeKHR choose_present_mode(const SwapchainSupport *support) {
    /* Prefer IMMEDIATE (no VSync, uncapped FPS) for benchmarking.
     * Fall back to MAILBOX (triple-buffered VSync), then FIFO (VSync). */
    for (u32 i = 0; i < support->present_mode_count; i++) {
        if (support->present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
            LOG_INFO("Present mode: IMMEDIATE (VSync off)");
            return VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }
    for (u32 i = 0; i < support->present_mode_count; i++) {
        if (support->present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
            LOG_INFO("Present mode: MAILBOX (triple-buffered VSync)");
            return VK_PRESENT_MODE_MAILBOX_KHR;
        }
    }
    LOG_INFO("Present mode: FIFO (VSync)");
    return VK_PRESENT_MODE_FIFO_KHR; /* guaranteed available */
}

static VkExtent2D choose_extent(const VkSurfaceCapabilitiesKHR *caps, i32 width, i32 height) {
    if (caps->currentExtent.width != UINT32_MAX) {
        return caps->currentExtent;
    }

    VkExtent2D extent = { (u32)width, (u32)height };
    extent.width  = ENGINE_CLAMP(extent.width,  caps->minImageExtent.width,  caps->maxImageExtent.width);
    extent.height = ENGINE_CLAMP(extent.height, caps->minImageExtent.height, caps->maxImageExtent.height);
    return extent;
}

/* --------------------------------------------------------------------------
 * Device suitability
 * ------------------------------------------------------------------------ */

static bool check_device_extension_support(VkPhysicalDevice device) {
    u32 count;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    VkExtensionProperties *available = malloc(sizeof(VkExtensionProperties) * count);
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, available);

    u32 found = 0;
    for (u32 i = 0; i < device_extension_count; i++) {
        for (u32 j = 0; j < count; j++) {
            if (strcmp(device_extensions[i], available[j].extensionName) == 0) {
                found++;
                break;
            }
        }
    }

    free(available);
    return found == device_extension_count;
}

static bool is_device_suitable(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices = find_queue_families(device, surface);
    if (!indices.has_graphics || !indices.has_present) return false;

    if (!check_device_extension_support(device)) return false;

    SwapchainSupport support = query_swapchain_support(device, surface);
    bool adequate = support.format_count > 0 && support.present_mode_count > 0;
    free_swapchain_support(&support);

    return adequate;
}

/* --------------------------------------------------------------------------
 * Public functions
 * ------------------------------------------------------------------------ */

EngineResult vk_create_instance(VulkanContext *ctx) {
#ifdef ENGINE_DEBUG
    if (!check_validation_layer_support()) {
        LOG_WARN("Validation layers requested but not available");
    }
#endif

    VkApplicationInfo app_info = {
        .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName   = "AI Game Engine",
        .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
        .pEngineName        = "AIEngine",
        .engineVersion      = VK_MAKE_VERSION(0, 1, 0),
        .apiVersion         = VK_API_VERSION_1_2,
    };

    /* Required extensions from GLFW + debug utils */
    u32 glfw_ext_count = 0;
    const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);

    u32 ext_count = glfw_ext_count;
#ifdef ENGINE_DEBUG
    ext_count++; /* VK_EXT_debug_utils */
#endif

    const char **extensions = malloc(sizeof(char *) * ext_count);
    for (u32 i = 0; i < glfw_ext_count; i++) {
        extensions[i] = glfw_exts[i];
    }
#ifdef ENGINE_DEBUG
    extensions[glfw_ext_count] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
#endif

    VkInstanceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo        = &app_info,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = extensions,
        .enabledLayerCount       = validation_layer_count,
        .ppEnabledLayerNames     = validation_layers,
    };

    VkResult result = vkCreateInstance(&create_info, NULL, &ctx->instance);
    free(extensions);

    if (result != VK_SUCCESS) {
        LOG_FATAL("vkCreateInstance failed: %d", result);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    LOG_INFO("Vulkan instance created (API 1.2)");
    return ENGINE_SUCCESS;
}

EngineResult vk_setup_debug_messenger(VulkanContext *ctx) {
#ifdef ENGINE_DEBUG
    VkDebugUtilsMessengerCreateInfoEXT create_info = {
        .sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity  = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
    };

    PFN_vkCreateDebugUtilsMessengerEXT func =
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx->instance,
            "vkCreateDebugUtilsMessengerEXT");

    if (!func) {
        LOG_WARN("vkCreateDebugUtilsMessengerEXT not available");
        return ENGINE_SUCCESS;
    }

    if (func(ctx->instance, &create_info, NULL, &ctx->debug_messenger) != VK_SUCCESS) {
        LOG_WARN("Failed to set up debug messenger");
    } else {
        LOG_DEBUG("Vulkan debug messenger enabled");
    }
#else
    ENGINE_UNUSED(ctx);
#endif
    return ENGINE_SUCCESS;
}

EngineResult vk_create_surface(VulkanContext *ctx, Window *window) {
    GLFWwindow *glfw_win = (GLFWwindow *)window_get_glfw_handle(window);
    if (glfwCreateWindowSurface(ctx->instance, glfw_win, NULL, &ctx->surface) != VK_SUCCESS) {
        LOG_FATAL("Failed to create Vulkan surface");
        return ENGINE_ERROR_VULKAN_SURFACE;
    }
    LOG_INFO("Vulkan surface created");
    return ENGINE_SUCCESS;
}

EngineResult vk_pick_physical_device(VulkanContext *ctx) {
    u32 count = 0;
    vkEnumeratePhysicalDevices(ctx->instance, &count, NULL);
    if (count == 0) {
        LOG_FATAL("No GPUs with Vulkan support found");
        return ENGINE_ERROR_VULKAN_DEVICE;
    }

    VkPhysicalDevice *devices = malloc(sizeof(VkPhysicalDevice) * count);
    vkEnumeratePhysicalDevices(ctx->instance, &count, devices);

    /* Pick the first suitable device, prefer discrete GPU */
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (u32 i = 0; i < count; i++) {
        if (is_device_suitable(devices[i], ctx->surface)) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(devices[i], &props);

            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                chosen = devices[i];
                LOG_INFO("Selected discrete GPU: %s", props.deviceName);
                break;
            }
            if (chosen == VK_NULL_HANDLE) {
                chosen = devices[i];
                LOG_INFO("Selected GPU: %s", props.deviceName);
            }
        }
    }

    free(devices);

    if (chosen == VK_NULL_HANDLE) {
        LOG_FATAL("No suitable GPU found");
        return ENGINE_ERROR_VULKAN_DEVICE;
    }

    ctx->physical_device = chosen;
    return ENGINE_SUCCESS;
}

EngineResult vk_create_logical_device(VulkanContext *ctx) {
    QueueFamilyIndices indices = find_queue_families(ctx->physical_device, ctx->surface);
    ctx->graphics_family = indices.graphics;
    ctx->present_family  = indices.present;

    /* Unique queue families */
    u32 unique_families[2] = { indices.graphics, indices.present };
    u32 unique_count = (indices.graphics == indices.present) ? 1 : 2;

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_infos[2];
    for (u32 i = 0; i < unique_count; i++) {
        queue_infos[i] = (VkDeviceQueueCreateInfo){
            .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = unique_families[i],
            .queueCount       = 1,
            .pQueuePriorities = &priority,
        };
    }

    VkPhysicalDeviceFeatures features = {0};

    VkDeviceCreateInfo create_info = {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount    = unique_count,
        .pQueueCreateInfos       = queue_infos,
        .pEnabledFeatures        = &features,
        .enabledExtensionCount   = device_extension_count,
        .ppEnabledExtensionNames = device_extensions,
        .enabledLayerCount       = validation_layer_count,
        .ppEnabledLayerNames     = validation_layers,
    };

    if (vkCreateDevice(ctx->physical_device, &create_info, NULL, &ctx->device) != VK_SUCCESS) {
        LOG_FATAL("Failed to create logical device");
        return ENGINE_ERROR_VULKAN_DEVICE;
    }

    vkGetDeviceQueue(ctx->device, indices.graphics, 0, &ctx->graphics_queue);
    vkGetDeviceQueue(ctx->device, indices.present,  0, &ctx->present_queue);

    LOG_INFO("Vulkan logical device created");
    return ENGINE_SUCCESS;
}

EngineResult vk_create_swapchain(VulkanContext *ctx, i32 width, i32 height) {
    SwapchainSupport support = query_swapchain_support(ctx->physical_device, ctx->surface);

    VkSurfaceFormatKHR format = choose_surface_format(&support);
    VkPresentModeKHR   mode   = choose_present_mode(&support);
    VkExtent2D         extent = choose_extent(&support.capabilities, width, height);

    u32 image_count = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        image_count > support.capabilities.maxImageCount) {
        image_count = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR create_info = {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = ctx->surface,
        .minImageCount    = image_count,
        .imageFormat      = format.format,
        .imageColorSpace  = format.colorSpace,
        .imageExtent      = extent,
        .imageArrayLayers = 1,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform     = support.capabilities.currentTransform,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode      = mode,
        .clipped          = VK_TRUE,
        .oldSwapchain     = VK_NULL_HANDLE,
    };

    QueueFamilyIndices indices = find_queue_families(ctx->physical_device, ctx->surface);
    u32 family_indices[] = { indices.graphics, indices.present };

    if (indices.graphics != indices.present) {
        create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
        create_info.queueFamilyIndexCount = 2;
        create_info.pQueueFamilyIndices   = family_indices;
    } else {
        create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
    }

    if (vkCreateSwapchainKHR(ctx->device, &create_info, NULL, &ctx->swapchain) != VK_SUCCESS) {
        free_swapchain_support(&support);
        LOG_FATAL("Failed to create swapchain");
        return ENGINE_ERROR_VULKAN_SWAPCHAIN;
    }

    /* Retrieve swapchain images */
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain, &ctx->swapchain_image_count, NULL);
    ctx->swapchain_images = malloc(sizeof(VkImage) * ctx->swapchain_image_count);
    vkGetSwapchainImagesKHR(ctx->device, ctx->swapchain,
        &ctx->swapchain_image_count, ctx->swapchain_images);

    ctx->swapchain_format = format.format;
    ctx->swapchain_extent = extent;

    free_swapchain_support(&support);
    LOG_INFO("Swapchain created: %ux%u, %u images", extent.width, extent.height, ctx->swapchain_image_count);
    return ENGINE_SUCCESS;
}

EngineResult vk_create_image_views(VulkanContext *ctx) {
    ctx->swapchain_image_views = malloc(sizeof(VkImageView) * ctx->swapchain_image_count);

    for (u32 i = 0; i < ctx->swapchain_image_count; i++) {
        VkImageViewCreateInfo view_info = {
            .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image    = ctx->swapchain_images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format   = ctx->swapchain_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
            .subresourceRange = {
                .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel   = 0,
                .levelCount     = 1,
                .baseArrayLayer = 0,
                .layerCount     = 1,
            },
        };

        if (vkCreateImageView(ctx->device, &view_info, NULL,
                              &ctx->swapchain_image_views[i]) != VK_SUCCESS) {
            LOG_FATAL("Failed to create image view %u", i);
            return ENGINE_ERROR_VULKAN_SWAPCHAIN;
        }
    }

    LOG_DEBUG("Created %u image views", ctx->swapchain_image_count);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Depth buffer
 * ------------------------------------------------------------------------ */

static u32 find_memory_type_init(VulkanContext *ctx, u32 type_filter, VkMemoryPropertyFlags props) {
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

EngineResult vk_create_depth_resources(VulkanContext *ctx) {
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;

    /* Create depth image */
    VkImageCreateInfo image_info = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent        = { ctx->swapchain_extent.width, ctx->swapchain_extent.height, 1 },
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .format        = depth_format,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
    };

    if (vkCreateImage(ctx->device, &image_info, NULL, &ctx->depth_image) != VK_SUCCESS) {
        LOG_FATAL("Failed to create depth image");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    /* Allocate device-local memory */
    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(ctx->device, ctx->depth_image, &mem_reqs);

    u32 mem_type = find_memory_type_init(ctx, mem_reqs.memoryTypeBits,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mem_type == UINT32_MAX) {
        vkDestroyImage(ctx->device, ctx->depth_image, NULL);
        return ENGINE_ERROR_VULKAN_INIT;
    }

    VkMemoryAllocateInfo alloc_info = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = mem_type,
    };

    if (vkAllocateMemory(ctx->device, &alloc_info, NULL, &ctx->depth_memory) != VK_SUCCESS) {
        LOG_FATAL("Failed to allocate depth image memory");
        vkDestroyImage(ctx->device, ctx->depth_image, NULL);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    vkBindImageMemory(ctx->device, ctx->depth_image, ctx->depth_memory, 0);

    /* Create image view */
    VkImageViewCreateInfo view_info = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = ctx->depth_image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = depth_format,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1,
        },
    };

    if (vkCreateImageView(ctx->device, &view_info, NULL, &ctx->depth_image_view) != VK_SUCCESS) {
        LOG_FATAL("Failed to create depth image view");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    LOG_INFO("Depth buffer created: %ux%u (D32_SFLOAT)",
             ctx->swapchain_extent.width, ctx->swapchain_extent.height);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Framebuffers (color + depth)
 * ------------------------------------------------------------------------ */

EngineResult vk_create_framebuffers(VulkanContext *ctx) {
    ctx->framebuffers = malloc(sizeof(VkFramebuffer) * ctx->swapchain_image_count);

    for (u32 i = 0; i < ctx->swapchain_image_count; i++) {
        VkImageView attachments[] = {
            ctx->swapchain_image_views[i],
            ctx->depth_image_view,
        };

        VkFramebufferCreateInfo fb_info = {
            .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass      = ctx->render_pass,
            .attachmentCount = 2,
            .pAttachments    = attachments,
            .width           = ctx->swapchain_extent.width,
            .height          = ctx->swapchain_extent.height,
            .layers          = 1,
        };

        if (vkCreateFramebuffer(ctx->device, &fb_info, NULL,
                                &ctx->framebuffers[i]) != VK_SUCCESS) {
            LOG_FATAL("Failed to create framebuffer %u", i);
            return ENGINE_ERROR_VULKAN_SWAPCHAIN;
        }
    }

    LOG_DEBUG("Created %u framebuffers", ctx->swapchain_image_count);
    return ENGINE_SUCCESS;
}

EngineResult vk_create_command_pool(VulkanContext *ctx) {
    VkCommandPoolCreateInfo pool_info = {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = ctx->graphics_family,
    };

    if (vkCreateCommandPool(ctx->device, &pool_info, NULL, &ctx->command_pool) != VK_SUCCESS) {
        LOG_FATAL("Failed to create command pool");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    LOG_DEBUG("Command pool created");
    return ENGINE_SUCCESS;
}

EngineResult vk_create_command_buffers(VulkanContext *ctx) {
    VkCommandBufferAllocateInfo alloc_info = {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = ctx->command_pool,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };

    if (vkAllocateCommandBuffers(ctx->device, &alloc_info, ctx->command_buffers) != VK_SUCCESS) {
        LOG_FATAL("Failed to allocate command buffers");
        return ENGINE_ERROR_VULKAN_INIT;
    }

    LOG_DEBUG("Allocated %d command buffers", MAX_FRAMES_IN_FLIGHT);
    return ENGINE_SUCCESS;
}

EngineResult vk_create_sync_objects(VulkanContext *ctx) {
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    };

    VkFenceCreateInfo fence_info = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT, /* start signaled so first frame doesn't hang */
    };

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(ctx->device, &sem_info, NULL, &ctx->image_available[i]) != VK_SUCCESS ||
            vkCreateSemaphore(ctx->device, &sem_info, NULL, &ctx->render_finished[i]) != VK_SUCCESS ||
            vkCreateFence(ctx->device, &fence_info, NULL, &ctx->in_flight[i]) != VK_SUCCESS) {
            LOG_FATAL("Failed to create sync objects for frame %u", i);
            return ENGINE_ERROR_VULKAN_INIT;
        }
    }

    ctx->current_frame = 0;
    LOG_DEBUG("Sync objects created (%d frames in flight)", MAX_FRAMES_IN_FLIGHT);
    return ENGINE_SUCCESS;
}

/* --------------------------------------------------------------------------
 * Cleanup
 * ------------------------------------------------------------------------ */

void vk_cleanup_swapchain(VulkanContext *ctx) {
    for (u32 i = 0; i < ctx->swapchain_image_count; i++) {
        vkDestroyFramebuffer(ctx->device, ctx->framebuffers[i], NULL);
    }
    free(ctx->framebuffers);
    ctx->framebuffers = NULL;

    /* Depth buffer */
    if (ctx->depth_image_view) {
        vkDestroyImageView(ctx->device, ctx->depth_image_view, NULL);
        ctx->depth_image_view = VK_NULL_HANDLE;
    }
    if (ctx->depth_image) {
        vkDestroyImage(ctx->device, ctx->depth_image, NULL);
        ctx->depth_image = VK_NULL_HANDLE;
    }
    if (ctx->depth_memory) {
        vkFreeMemory(ctx->device, ctx->depth_memory, NULL);
        ctx->depth_memory = VK_NULL_HANDLE;
    }

    for (u32 i = 0; i < ctx->swapchain_image_count; i++) {
        vkDestroyImageView(ctx->device, ctx->swapchain_image_views[i], NULL);
    }
    free(ctx->swapchain_image_views);
    ctx->swapchain_image_views = NULL;

    free(ctx->swapchain_images);
    ctx->swapchain_images = NULL;

    vkDestroySwapchainKHR(ctx->device, ctx->swapchain, NULL);
    ctx->swapchain = VK_NULL_HANDLE;
}

void vk_destroy(VulkanContext *ctx) {
    vkDeviceWaitIdle(ctx->device);

    for (u32 i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        vkDestroySemaphore(ctx->device, ctx->image_available[i], NULL);
        vkDestroySemaphore(ctx->device, ctx->render_finished[i], NULL);
        vkDestroyFence(ctx->device, ctx->in_flight[i], NULL);
    }

    vkDestroyCommandPool(ctx->device, ctx->command_pool, NULL);

    vk_cleanup_swapchain(ctx);

    if (ctx->vertex_buffer) {
        vkDestroyBuffer(ctx->device, ctx->vertex_buffer, NULL);
        vkFreeMemory(ctx->device, ctx->vertex_buffer_memory, NULL);
    }

    vkDestroyPipeline(ctx->device, ctx->graphics_pipeline, NULL);
    vkDestroyPipelineLayout(ctx->device, ctx->pipeline_layout, NULL);

    if (ctx->text_pipeline) {
        vkDestroyPipeline(ctx->device, ctx->text_pipeline, NULL);
    }
    if (ctx->text_pipeline_layout) {
        vkDestroyPipelineLayout(ctx->device, ctx->text_pipeline_layout, NULL);
    }
    if (ctx->text_desc_set_layout) {
        vkDestroyDescriptorSetLayout(ctx->device, ctx->text_desc_set_layout, NULL);
    }

    /* Geometry texture descriptors */
    if (ctx->geo_desc_pool) {
        vkDestroyDescriptorPool(ctx->device, ctx->geo_desc_pool, NULL);
    }
    if (ctx->geo_desc_set_layout) {
        vkDestroyDescriptorSetLayout(ctx->device, ctx->geo_desc_set_layout, NULL);
    }

    vkDestroyRenderPass(ctx->device, ctx->render_pass, NULL);

    vkDestroyDevice(ctx->device, NULL);

#ifdef ENGINE_DEBUG
    if (ctx->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT func =
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(ctx->instance,
                "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(ctx->instance, ctx->debug_messenger, NULL);
        }
    }
#endif

    vkDestroySurfaceKHR(ctx->instance, ctx->surface, NULL);
    vkDestroyInstance(ctx->instance, NULL);

    LOG_INFO("Vulkan resources destroyed");
}
