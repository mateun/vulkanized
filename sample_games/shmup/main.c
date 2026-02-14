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

#define NUM_ENEMIES  29
#define MAX_BULLETS  64
#define BULLET_SPEED 25.0f
#define SHIP_SPEED   15.0f
#define TRAIL_LENGTH 12       /* number of ghost afterimages */

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

    /* Triangle mesh (player) — tip points up (+Y) */
    static const Vertex tri_verts[] = {
        { .position = {  0.0f,  0.5f }, .uv = { 0.5f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f, -0.5f }, .uv = { 0.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f, -0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
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

    /* Bullet mesh — tiny elongated quad */
    static const Vertex bullet_verts[] = {
        { .position = { -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f, -0.5f }, .uv = { 1.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f, -0.5f }, .uv = { 0.0f, 0.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = {  0.5f,  0.5f }, .uv = { 1.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
        { .position = { -0.5f,  0.5f }, .uv = { 0.0f, 1.0f }, .color = { 1.0f, 1.0f, 1.0f } },
    };

    MeshHandle mesh_bullet;
    if (renderer_upload_mesh(renderer, bullet_verts,
                              ENGINE_ARRAY_LEN(bullet_verts), &mesh_bullet) != ENGINE_SUCCESS) {
        LOG_FATAL("Failed to upload bullet mesh");
        renderer_destroy(renderer);
        window_destroy(window);
        return 1;
    }

    /* ---- Enable bloom (80s arcade neon glow) ---- */
    renderer_set_bloom(renderer, true, 0.8f, 0.6f);

    /* ---- Generate instances ---- */
    srand((unsigned)time(NULL));

    /* Neon color palette — HDR values > 1.0 glow through the bloom threshold */
    static const f32 neon_colors[][3] = {
        { 2.0f, 0.0f, 1.5f },  /* hot pink / magenta */
        { 0.0f, 1.8f, 2.0f },  /* electric cyan */
        { 2.0f, 1.0f, 0.0f },  /* neon orange */
        { 0.0f, 2.0f, 0.5f },  /* neon green */
        { 1.5f, 0.0f, 2.0f },  /* purple */
        { 2.0f, 0.2f, 0.2f },  /* neon red */
    };
    static const int num_neon = sizeof(neon_colors) / sizeof(neon_colors[0]);

    /* Enemies (quads) — each gets a random neon color */
    InstanceData enemies[NUM_ENEMIES];
    for (int i = 0; i < NUM_ENEMIES; i++) {
        enemies[i].position[0] = ((f32)rand() / RAND_MAX) * 30.0f - 15.0f;
        enemies[i].position[1] = ((f32)rand() / RAND_MAX) * 16.0f - 8.0f;
        enemies[i].rotation    = ((f32)rand() / RAND_MAX) * 6.2831853f;
        enemies[i].scale[0]    = 2.0f;
        enemies[i].scale[1]    = 2.0f;
        int ci = rand() % num_neon;
        enemies[i].color[0]    = neon_colors[ci][0];
        enemies[i].color[1]    = neon_colors[ci][1];
        enemies[i].color[2]    = neon_colors[ci][2];
    }

    /* Player (triangle, bright cyan — HDR so it glows) */
    InstanceData player = {
        .position = { 0.0f, 0.0f },
        .rotation = 0.0f,
        .scale    = { 2.0f, 2.0f },
        .color    = { 0.2f, 1.8f, 2.0f },
    };

    /* ---- Bullet pool ---- */
    InstanceData bullets[MAX_BULLETS];
    int num_bullets = 0;

    /* ---- Ghost trail ring buffer ---- */
    f32 trail_pos[TRAIL_LENGTH][2];  /* recorded positions */
    int trail_head  = 0;             /* next write index */
    int trail_count = 0;             /* how many slots filled so far */
    f32 trail_timer = 0.0f;          /* accumulator for spacing snapshots */
    const f32 trail_interval = 0.02f; /* record every 20ms (~50 Hz) */
    /* Pre-fill with player start so the trail doesn't flicker at launch */
    for (int i = 0; i < TRAIL_LENGTH; i++) {
        trail_pos[i][0] = player.position[0];
        trail_pos[i][1] = player.position[1];
    }
    InstanceData trail_instances[TRAIL_LENGTH];

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

        // Player input
        if (input_key_down(GLFW_KEY_A)) {
            player.position[0] -= SHIP_SPEED * delta_time;
        }
        if (input_key_down(GLFW_KEY_D)) {
            player.position[0] += SHIP_SPEED * delta_time;
        }
        if (input_key_down(GLFW_KEY_W)) {
            player.position[1] += SHIP_SPEED * delta_time;
        }
        if (input_key_down(GLFW_KEY_S)) {
            player.position[1] -= SHIP_SPEED * delta_time;
        }
        /* Shoot on left click */
        if (input_mouse_pressed(GLFW_MOUSE_BUTTON_LEFT) && num_bullets < MAX_BULLETS) {
            InstanceData *b = &bullets[num_bullets++];
            /* Spawn at the tip of the player triangle (top of the scaled mesh) */
            b->position[0] = player.position[0];
            b->position[1] = player.position[1] + player.scale[1] * 0.5f;
            b->rotation    = 0.0f;
            b->scale[0]    = 0.15f;  /* narrow */
            b->scale[1]    = 0.6f;   /* elongated */
            b->color[0]    = 2.5f;   /* bright yellow-white HDR glow */
            b->color[1]    = 2.0f;
            b->color[2]    = 0.5f;
        }

        /* Move bullets upward, remove ones that go off screen */
        for (int i = 0; i < num_bullets; ) {
            bullets[i].position[1] += BULLET_SPEED * delta_time;
            /* Remove if way above visible area (half_height/zoom ≈ 15) */
            if (bullets[i].position[1] > 20.0f) {
                bullets[i] = bullets[--num_bullets]; /* swap-remove */
            } else {
                i++;
            }
        }

        /* Handle resize */
        if (window_was_resized(window)) {
            window_reset_resized(window);
            renderer_handle_resize(renderer);
        }

        /* Spin enemies */
        for (int i = 0; i < NUM_ENEMIES; i++) {
            enemies[i].rotation += 1.5f * delta_time; /* ~90 deg/sec */
        }

        /* Record player position into trail ring buffer (time-gated) */
        trail_timer += delta_time;
        while (trail_timer >= trail_interval) {
            trail_timer -= trail_interval;
            trail_pos[trail_head][0] = player.position[0];
            trail_pos[trail_head][1] = player.position[1];
            trail_head = (trail_head + 1) % TRAIL_LENGTH;
            if (trail_count < TRAIL_LENGTH) trail_count++;
        }

        /* Build ghost trail instances — oldest first, fading out */
        int trail_draw_count = 0;
        for (int i = 0; i < trail_count; i++) {
            /* Read from oldest to newest: oldest = trail_head, newest = trail_head-1 */
            int idx = (trail_head + i) % TRAIL_LENGTH;
            /* t goes from 0.0 (oldest) to 1.0 (newest) */
            f32 t = (f32)i / (f32)trail_count;
            /* Skip the very newest — that's the player itself */
            if (i == trail_count - 1) continue;
            InstanceData *g = &trail_instances[trail_draw_count++];
            g->position[0] = trail_pos[idx][0];
            g->position[1] = trail_pos[idx][1];
            g->rotation    = player.rotation;
            /* Shrink older ghosts */
            f32 ghost_scale = 0.4f + 0.6f * t;
            g->scale[0]    = player.scale[0] * ghost_scale;
            g->scale[1]    = player.scale[1] * ghost_scale;
            /* Fade color: older = dimmer, but still HDR so bloom catches them */
            f32 fade = t * t; /* quadratic fade — sharper falloff */
            g->color[0]    = player.color[0] * fade * 0.6f;
            g->color[1]    = player.color[1] * fade * 0.6f;
            g->color[2]    = player.color[2] * fade * 0.6f;
        }

        /* ---- Frame ---- */
        renderer_begin_frame(renderer);
        renderer_set_camera(renderer, camera);

        /* Draw ghost trail (behind player), then enemies, player, bullets */
        if (trail_draw_count > 0) {
            renderer_draw_mesh(renderer, mesh_triangle, trail_instances, (u32)trail_draw_count);
        }
        renderer_draw_mesh_textured(renderer, mesh_quad, heroTexture, enemies, NUM_ENEMIES);
        renderer_draw_mesh(renderer, mesh_triangle, &player, 1);
        if (num_bullets > 0) {
            renderer_draw_mesh(renderer, mesh_bullet, bullets, (u32)num_bullets);
        }

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
