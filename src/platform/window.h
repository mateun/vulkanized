#ifndef ENGINE_WINDOW_H
#define ENGINE_WINDOW_H

#include "core/common.h"

/* Opaque forward declaration — actual GLFW handle stays in .c */
typedef struct Window Window;

typedef struct {
    const char *title;
    i32         width;
    i32         height;
    bool        resizable;
} WindowConfig;

EngineResult window_create(const WindowConfig *config, Window **out_window);
void         window_destroy(Window *window);
bool         window_should_close(const Window *window);
void         window_poll_events(void);

/* Vulkan needs the raw GLFW window to create a surface */
void        *window_get_glfw_handle(const Window *window);

/* Get current framebuffer size (may differ from window size on HiDPI) */
void         window_get_framebuffer_size(const Window *window, i32 *width, i32 *height);

/* Flag that the framebuffer was resized (checked and cleared by renderer) */
bool         window_was_resized(Window *window);
void         window_reset_resized(Window *window);

#endif /* ENGINE_WINDOW_H */
