#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

#include "core/common.h"

typedef struct Window Window; /* forward decl */

/* Initialize input system. Must be called after window creation. */
void input_init(Window *window);

/* Call once per frame BEFORE polling events to advance key state.
 * This copies "current" into "previous" so pressed/released detection works. */
void input_update(void);

/* Is the key currently held down? (continuous — use for movement) */
bool input_key_down(int glfw_key);

/* Was the key pressed this frame? (single trigger — use for actions) */
bool input_key_pressed(int glfw_key);

/* Was the key released this frame? */
bool input_key_released(int glfw_key);

/* ---- Mouse buttons ---- */

/* Is the mouse button currently held down? */
bool input_mouse_down(int glfw_button);

/* Was the mouse button pressed this frame? */
bool input_mouse_pressed(int glfw_button);

/* Was the mouse button released this frame? */
bool input_mouse_released(int glfw_button);

#endif /* ENGINE_INPUT_H */
