#include "core/common.h"
#include "core/log.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_ENEMIES 29

int main(void) {
    /* ---- Init logging ---- */
#ifdef ENGINE_DEBUG
    log_init(LOG_LEVEL_TRACE);
#else
    log_init(LOG_LEVEL_INFO);
#endif

    LOG_INFO("SHMUP starting...");

    /* ---- Create window ---- */
    WindowConfig win_config = {
        .title     = "SHMUP",
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
        .font_path = "assets/consolas.ttf",
        .font_size = 24.0f,
        .clear_color = {0, 0, 0, 1}
    };

    Renderer *renderer = NULL;
    if (renderer_create(window, &render_config, &renderer) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to create renderer");
        window_destroy(window);
        return 1;
    }

    // Load some textures:
    TextureHandle  heroTexture;
    renderer_load_texture(renderer, "assets/blob.png", &heroTexture);

    Camera2D *camera = (Camera2D *)calloc(1, sizeof(Camera2D));
    camera->zoom        = 2.0f;
    camera->half_height = 30.0f;

    /* ---- Upload meshes ---- */

    /* Triangle mesh (player) */
    static const Vertex tri_verts[] = {
        { .position = {  0.0f, -0.5f }, .uv = { 0.5f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f,  0.5f }, .uv = { 0.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
    };

    MeshHandle mesh_triangle;
    if (renderer_upload_mesh(renderer, tri_verts,
                              ENGINE_ARRAY_LEN(tri_verts), &mesh_triangle) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to upload triangle mesh");
        renderer_destroy(renderer);
        window_destroy(window);
        return 1;
    }

    /* Quad mesh (enemies) — two triangles forming a rectangle */
    static const Vertex quad_verts[] = {
        { .position = { -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f, -0.5f }, .uv = { 1.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f,  0.5f }, .uv = { 0.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
    };

    MeshHandle mesh_quad;
    if (renderer_upload_mesh(renderer, quad_verts,
                              ENGINE_ARRAY_LEN(quad_verts), &mesh_quad) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to upload quad mesh");
        renderer_destroy(renderer);
        window_destroy(window);
        return 1;
    }

    /* ---- Generate instances ---- */
    srand((unsigned)time(NULL));

    /* Enemies (quads, red) */
    InstanceData enemies[NUM_ENEMIES];
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemies[i].position[0] = ((f32)rand() / RAND_MAX) * 30.0f - 15.0f;
        enemies[i].position[1] = ((f32)rand() / RAND_MAX) * 16.0f - 8.0f;
        enemies[i].rotation    = ((f32)rand() / RAND_MAX) * 6.2831853f;
        enemies[i].scale[0]    = 2.0f;
        enemies[i].scale[1]    = 2.0f;
        enemies[i].color[0]    = 1.0f;
        enemies[i].color[1]    = 1.0f;
        enemies[i].color[2]    = 1.0f;
    }

    /* Player (triangle, white) */
    InstanceData player = {
        .position = { 0.0f, 0.0f },
        .rotation = 0.0f,
        .scale    = { 2.0f, 2.0f },
        .color    = { 0.9f, 0.9f, 0.9f },
    };

    /* ---- Main loop ---- */
    LOG_INFO("Entering main loop");

    f64 last_time = glfwGetTime();

    while (!window_should_close(window)) {
        /* Frame timing */
        f64 current_time = glfwGetTime();
        f32 delta_time = (f32)(current_time - last_time);
        last_time = current_time;

        input_update();
        window_poll_events();

        /* ESC to close */
        if (input_key_pressed(GLFW_KEY_ESCAPE)) {
            break;
        }

        /* Handle resize */
        if (window_was_resized(window)) {
            window_reset_resized(window);
            renderer_handle_resize(renderer);
        }

        /* ---- Frame ---- */
        renderer_begin_frame(renderer);
        renderer_set_camera(renderer, camera);

        /* Draw enemies (quads) and player (triangle) */
        renderer_draw_mesh_textured(renderer, mesh_quad, heroTexture, enemies, NUM_ENEMIES);
        renderer_draw_mesh(renderer, mesh_triangle, &player, 1);

        /* Title text */
        renderer_draw_text(renderer, "SCORE", 10.0f, 10.0f, 1.0f,
                           1.0f, 1.0f, 1.0f);

        // Render score
        {
            int32_t score = 849789;
            char ft_buf[32];
            snprintf(ft_buf, sizeof(ft_buf), "%d", score);

            f32 ft_scale = 11.0f / 24.0f;
            f32 char_width = 14.0f * ft_scale;
            f32 text_width = (f32)strlen(ft_buf) * char_width;

            u32 extent_w, extent_h;
            renderer_get_extent(renderer, &extent_w, &extent_h);

            f32 ft_x = (f32)300.0f;
            f32 ft_y = 10.0f;

            renderer_draw_text(renderer, ft_buf, ft_x, ft_y, 1.0f,
                               1.0f, 1.0f, 1.0f);
        }
        renderer_draw_text(renderer, "Press ESC to quit", 10.0f, 40.0f, 1.0f,
                           0.7f, 0.7f, 0.7f);

        /* Frametime display (11px, top-right corner) */
        {
            f32 ft_ms = delta_time * 1000.0f;
            char ft_buf[32];
            snprintf(ft_buf, sizeof(ft_buf), "%.3f ms", (double)ft_ms);

            f32 ft_scale = 11.0f / 24.0f;
            f32 char_width = 14.0f * ft_scale;
            f32 text_width = (f32)strlen(ft_buf) * char_width;

            u32 extent_w, extent_h;
            renderer_get_extent(renderer, &extent_w, &extent_h);

            f32 ft_x = (f32)extent_w - text_width - 10.0f;
            f32 ft_y = 10.0f;

            renderer_draw_text(renderer, ft_buf, ft_x, ft_y, ft_scale,
                               0.0f, 1.0f, 0.0f);
        }

        renderer_end_frame(renderer);
    }

    /* ---- Cleanup ---- */
    LOG_INFO("Shutting down...");
    renderer_destroy(renderer);
    window_destroy(window);

    LOG_INFO("Goodbye!");
    return 0;
}
