#include "core/common.h"
#include "core/log.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void) {
#ifdef ENGINE_DEBUG
    log_init(LOG_LEVEL_TRACE);
#else
    log_init(LOG_LEVEL_INFO);
#endif

    LOG_INFO("Cube Demo starting...");

    /* ---- Create window ---- */
    WindowConfig win_config = {
        .title     = "3D Cube Demo",
        .width     = 1280,
        .height    = 720,
        .resizable = true,
    };

    Window *window = NULL;
    if (window_create(&win_config, &window) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to create window");
        return 1;
    }

    input_init(window);

    /* ---- Create renderer ---- */
    RendererConfig render_config = {
        .font_path   = "assets/consolas.ttf",
        .font_size   = 24.0f,
        .clear_color = { 0.05f, 0.05f, 0.08f, 1.0f },
    };

    Renderer *renderer = NULL;
    if (renderer_create(window, &render_config, &renderer) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to create renderer");
        window_destroy(window);
        return 1;
    }

    /* ---- Create 3D primitives ---- */
    MeshHandle mesh_cube, mesh_sphere, mesh_cylinder;
    renderer_create_cube(renderer, &mesh_cube);
    renderer_create_sphere(renderer, 32, 16, &mesh_sphere);
    renderer_create_cylinder(renderer, 24, &mesh_cylinder);

    /* ---- Load glTF model ---- */
    MeshHandle mesh_duck;
    bool has_duck = (renderer_load_model(renderer, "assets/duck.glb", &mesh_duck) == ENGINE_SUCCESS);

    /* ---- Frame timing ---- */
    f64 last_time = glfwGetTime();
    f32 total_time = 0.0f;

    /* ---- Main loop ---- */
    while (!window_should_close(window)) {
        window_poll_events();

        /* Frame timing */
        f64 now = glfwGetTime();
        f32 dt = (f32)(now - last_time);
        last_time = now;
        total_time += dt;

        /* ESC to quit */
        if (input_key_pressed(GLFW_KEY_ESCAPE)) break;

        /* ---- Camera: orbit around origin ---- */
        f32 cam_dist  = 8.0f;
        f32 cam_angle = total_time * 0.3f;
        f32 cam_height = 4.0f;

        Camera3D camera = {
            .position   = { sinf(cam_angle) * cam_dist, cam_height, cosf(cam_angle) * cam_dist },
            .target     = { 0.0f, 0.0f, 0.0f },
            .up         = { 0.0f, 1.0f, 0.0f },
            .fov        = 60.0f,
            .near_plane = 0.1f,
            .far_plane  = 100.0f,
        };

        /* ---- Directional light ---- */
        DirectionalLight light = {
            .direction = { 0.3f, -1.0f, 0.5f },
            .color     = { 1.0f,  0.95f, 0.9f },
            .ambient   = { 0.15f, 0.15f, 0.2f },
            .shininess = 32.0f,
        };

        /* ---- Instance data for the primitives ---- */
        InstanceData3D cube_inst = {
            .position = { -3.0f, 0.0f, 0.0f },
            .rotation = { total_time * 0.5f, total_time * 0.7f, 0.0f },
            .scale    = { 1.0f, 1.0f, 1.0f },
            .color    = { 0.8f, 0.3f, 0.2f }, /* red */
        };

        InstanceData3D sphere_inst = {
            .position = { -1.0f, 0.0f, 0.0f },
            .rotation = { 0.0f, total_time * 0.4f, 0.0f },
            .scale    = { 1.5f, 1.5f, 1.5f },
            .color    = { 0.2f, 0.6f, 0.9f }, /* blue */
        };

        InstanceData3D cylinder_inst = {
            .position = { 1.5f, 0.0f, 0.0f },
            .rotation = { total_time * 0.3f, total_time * 0.5f, total_time * 0.6f },
            .scale    = { 1.0f, 1.5f, 1.0f },
            .color    = { 0.3f, 0.8f, 0.3f }, /* green */
        };

        InstanceData3D duck_inst = {
            .position = { 4.0f, 0.0f, 0.0f },
            .rotation = { 0.0f, total_time * 0.5f, 0.0f },
            .scale    = { 0.01f, 0.01f, 0.01f }, /* Duck model is large, scale down */
            .color    = { 0.9f, 0.8f, 0.2f }, /* yellow */
        };

        /* ---- Render ---- */
        if (renderer_begin_frame(renderer) != ENGINE_SUCCESS) continue;

        renderer_set_camera_3d(renderer, &camera);
        renderer_set_light(renderer, &light);

        renderer_draw_mesh_3d(renderer, mesh_cube, &cube_inst, 1);
        renderer_draw_mesh_3d(renderer, mesh_sphere, &sphere_inst, 1);
        renderer_draw_mesh_3d(renderer, mesh_cylinder, &cylinder_inst, 1);
        if (has_duck) {
            renderer_draw_mesh_3d(renderer, mesh_duck, &duck_inst, 1);
        }

        /* Text overlay */
        char fps_str[64];
        snprintf(fps_str, sizeof(fps_str), "dt: %.2f ms", dt * 1000.0f);
        renderer_draw_text(renderer, "3D CUBE DEMO", 10, 10, 1.0f, 1, 1, 1);
        renderer_draw_text(renderer, fps_str, 10, 40, 0.7f, 0, 1, 0);
        renderer_draw_text(renderer, "ESC to quit", 10, 65, 0.6f, 0.5f, 0.5f, 0.5f);

        renderer_end_frame(renderer);

        input_update();
    }

    /* ---- Cleanup ---- */
    renderer_destroy(renderer);
    window_destroy(window);

    LOG_INFO("Cube Demo finished");
    return 0;
}
