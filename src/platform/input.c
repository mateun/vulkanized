#include "platform/input.h"
#include "platform/window.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string.h>

/* We track two frames of key state: previous and current.
 * "pressed"  = current is down AND previous was up
 * "released" = current is up   AND previous was down
 * "down"     = current is down
 *
 * GLFW_KEY_LAST is 348, so a small static array is fine. */

#define KEY_COUNT   (GLFW_KEY_LAST + 1)
#define MOUSE_COUNT (GLFW_MOUSE_BUTTON_LAST + 1)

static bool s_keys_current[KEY_COUNT];
static bool s_keys_previous[KEY_COUNT];

static bool s_mouse_current[MOUSE_COUNT];
static bool s_mouse_previous[MOUSE_COUNT];

static GLFWwindow *s_glfw_window = NULL;

/* GLFW key callback — fires on press, release, and repeat */
static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;

    if (key < 0 || key >= KEY_COUNT) return;

    if (action == GLFW_PRESS) {
        s_keys_current[key] = true;
    } else if (action == GLFW_RELEASE) {
        s_keys_current[key] = false;
    }
    /* GLFW_REPEAT is ignored — we handle repeat via held-down state */
}

/* GLFW mouse button callback */
static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods) {
    (void)window;
    (void)mods;

    if (button < 0 || button >= MOUSE_COUNT) return;

    if (action == GLFW_PRESS) {
        s_mouse_current[button] = true;
    } else if (action == GLFW_RELEASE) {
        s_mouse_current[button] = false;
    }
}

void input_init(Window *window) {
    s_glfw_window = (GLFWwindow *)window_get_glfw_handle(window);
    memset(s_keys_current,  0, sizeof(s_keys_current));
    memset(s_keys_previous, 0, sizeof(s_keys_previous));
    memset(s_mouse_current,  0, sizeof(s_mouse_current));
    memset(s_mouse_previous, 0, sizeof(s_mouse_previous));

    glfwSetKeyCallback(s_glfw_window, key_callback);
    glfwSetMouseButtonCallback(s_glfw_window, mouse_button_callback);
}

void input_update(void) {
    /* Snapshot current state into previous before GLFW updates current */
    memcpy(s_keys_previous, s_keys_current, sizeof(s_keys_current));
    memcpy(s_mouse_previous, s_mouse_current, sizeof(s_mouse_current));
}

bool input_key_down(int glfw_key) {
    if (glfw_key < 0 || glfw_key >= KEY_COUNT) return false;
    return s_keys_current[glfw_key];
}

bool input_key_pressed(int glfw_key) {
    if (glfw_key < 0 || glfw_key >= KEY_COUNT) return false;
    return s_keys_current[glfw_key] && !s_keys_previous[glfw_key];
}

bool input_key_released(int glfw_key) {
    if (glfw_key < 0 || glfw_key >= KEY_COUNT) return false;
    return !s_keys_current[glfw_key] && s_keys_previous[glfw_key];
}

/* ---- Mouse buttons ---- */

bool input_mouse_down(int glfw_button) {
    if (glfw_button < 0 || glfw_button >= MOUSE_COUNT) return false;
    return s_mouse_current[glfw_button];
}

bool input_mouse_pressed(int glfw_button) {
    if (glfw_button < 0 || glfw_button >= MOUSE_COUNT) return false;
    return s_mouse_current[glfw_button] && !s_mouse_previous[glfw_button];
}

bool input_mouse_released(int glfw_button) {
    if (glfw_button < 0 || glfw_button >= MOUSE_COUNT) return false;
    return !s_mouse_current[glfw_button] && s_mouse_previous[glfw_button];
}
