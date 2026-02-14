#include "platform/window.h"
#include "core/log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <stdlib.h>

struct Window {
    GLFWwindow *handle;
    bool        framebuffer_resized;
};

/* ---- GLFW callbacks ---- */

static void framebuffer_resize_cb(GLFWwindow *glfw_win, int width, int height) {
    ENGINE_UNUSED(width);
    ENGINE_UNUSED(height);
    Window *win = (Window *)glfwGetWindowUserPointer(glfw_win);
    if (win) {
        win->framebuffer_resized = true;
    }
}

/* ---- Public API ---- */

EngineResult window_create(const WindowConfig *config, Window **out_window) {
    if (!glfwInit()) {
        LOG_FATAL("Failed to initialize GLFW");
        return ENGINE_ERROR_WINDOW_INIT;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); /* Vulkan, no OpenGL */
    glfwWindowHint(GLFW_RESIZABLE, config->resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow *handle = glfwCreateWindow(
        config->width, config->height, config->title, NULL, NULL);

    if (!handle) {
        LOG_FATAL("Failed to create GLFW window");
        glfwTerminate();
        return ENGINE_ERROR_WINDOW_INIT;
    }

    Window *win = malloc(sizeof(Window));
    if (!win) {
        glfwDestroyWindow(handle);
        glfwTerminate();
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    win->handle = handle;
    win->framebuffer_resized = false;

    glfwSetWindowUserPointer(handle, win);
    glfwSetFramebufferSizeCallback(handle, framebuffer_resize_cb);

    LOG_INFO("Window created: %dx%d \"%s\"", config->width, config->height, config->title);

    *out_window = win;
    return ENGINE_SUCCESS;
}

void window_destroy(Window *window) {
    if (window) {
        if (window->handle) {
            glfwDestroyWindow(window->handle);
        }
        free(window);
    }
    glfwTerminate();
}

bool window_should_close(const Window *window) {
    return glfwWindowShouldClose(window->handle);
}

void window_poll_events(void) {
    glfwPollEvents();
}

void *window_get_glfw_handle(const Window *window) {
    return window->handle;
}

void window_get_framebuffer_size(const Window *window, i32 *width, i32 *height) {
    glfwGetFramebufferSize(window->handle, width, height);
}

bool window_was_resized(Window *window) {
    return window->framebuffer_resized;
}

void window_reset_resized(Window *window) {
    window->framebuffer_resized = false;
}
