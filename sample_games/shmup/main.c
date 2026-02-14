#include "core/common.h"
#include "core/log.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"
#include "gameplay/collision.h"
#include "gameplay/particles.h"
#include "audio/audio.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h> 

#define MAX_ENEMIES    29
#define MAX_BULLETS    64
#define MAX_HIT_PAIRS  64
#define BULLET_SPEED   25.0f
#define SHIP_SPEED     15.0f
#define TRAIL_LENGTH   12       /* number of ghost afterimages */
#define ENEMY_RADIUS   0.8f     /* collision radius for enemies (scale 2 × half-extent 0.5 × ~0.8) */
#define BULLET_RADIUS  0.15f    /* collision radius for bullets (tiny) */
#define PLAYER_RADIUS  0.7f     /* collision radius for player ship */
#define MAX_PARTICLES  1024     /* particle budget for explosions */

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

    /* ---- Create audio engine ---- */
    AudioEngine *audio = NULL;
    if (audio_init(&audio) != ENGINE_SUCCESS) {
        LOG_WARN("Failed to init audio — continuing without sound");
    }

    /* Load sound effects */
    SoundHandle snd_shoot     = {0};
    SoundHandle snd_explosion = {0};
    bool has_audio = (audio != NULL);
    if (has_audio) {
        if (audio_load_sound(audio, "assets/shoot.wav", &snd_shoot) != ENGINE_SUCCESS)
            LOG_WARN("Could not load shoot.wav");
        if (audio_load_sound(audio, "assets/explosion.wav", &snd_explosion) != ENGINE_SUCCESS)
            LOG_WARN("Could not load explosion.wav");
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
    InstanceData enemies[MAX_ENEMIES];
    int num_enemies = MAX_ENEMIES;
    for (int i = 0; i < num_enemies; i++) {
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

    /* ---- Game state ---- */
    i32 score = 0;
    bool player_hit = false;
    f32  hit_flash_timer = 0.0f;  /* brief flash on player hit */

    /* ---- Bullet pool ---- */
    InstanceData bullets[MAX_BULLETS];
    int num_bullets = 0;
    CollisionPair hit_pairs[MAX_HIT_PAIRS];

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

    /* ---- Particle pool ---- */
    Particle particles[MAX_PARTICLES];
    i32 num_particles = 0;
    InstanceData particle_instances[MAX_PARTICLES];

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
            if (has_audio) audio_play_sound(audio, snd_shoot, false, 0.5f);
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

        /* ---- Collision: bullets vs enemies ---- */
        {
            i32 num_hits = collision_instances_vs_instances(
                bullets, num_bullets, BULLET_RADIUS,
                enemies, num_enemies, ENEMY_RADIUS,
                hit_pairs, MAX_HIT_PAIRS);

            /* Mark hit bullets and enemies for removal (flag with NaN position) */
            bool bullet_dead[MAX_BULLETS] = {false};
            bool enemy_dead[MAX_ENEMIES]  = {false};
            for (i32 h = 0; h < num_hits; h++) {
                bullet_dead[hit_pairs[h].index_a] = true;
                enemy_dead[hit_pairs[h].index_b]  = true;
                score += 100;
            }

            /* Spawn explosions at dead enemies, then swap-remove them */
            for (int i = num_enemies - 1; i >= 0; i--) {
                if (enemy_dead[i]) {
                    ParticleEmitter explosion = {
                        .position = { enemies[i].position[0], enemies[i].position[1] },
                        .color    = { enemies[i].color[0], enemies[i].color[1], enemies[i].color[2] },
                        .count              = 24,
                        .speed_min          = 3.0f,
                        .speed_max          = 10.0f,
                        .lifetime_min       = 0.3f,
                        .lifetime_max       = 0.8f,
                        .scale              = 0.4f,
                        .angular_velocity_min = -5.0f,
                        .angular_velocity_max =  5.0f,
                    };
                    i32 emitted = particles_emit(&explosion, particles, num_particles, MAX_PARTICLES);
                    num_particles += emitted;

                    if (has_audio) audio_play_sound(audio, snd_explosion, false, 0.7f);

                    enemies[i] = enemies[--num_enemies];
                }
            }

            /* Swap-remove dead bullets */
            for (int i = num_bullets - 1; i >= 0; i--) {
                if (bullet_dead[i]) {
                    bullets[i] = bullets[--num_bullets];
                }
            }
        }

        /* ---- Collision: enemies vs player ---- */
        if (!player_hit) {
            i32 hit_idx = collision_circle_vs_instances(
                player.position[0], player.position[1], PLAYER_RADIUS,
                enemies, num_enemies, ENEMY_RADIUS);

            if (hit_idx >= 0) {
                player_hit = true;
                hit_flash_timer = 0.5f; /* flash for 0.5 seconds */
                LOG_INFO("Player hit by enemy %d!", hit_idx);
            }
        }

        /* Flash timer countdown */
        if (hit_flash_timer > 0.0f) {
            hit_flash_timer -= delta_time;
            if (hit_flash_timer <= 0.0f) {
                hit_flash_timer = 0.0f;
                player_hit = false;
                /* Restore player color */
                player.color[0] = 0.2f;
                player.color[1] = 1.8f;
                player.color[2] = 2.0f;
            } else {
                /* Flash red/white */
                f32 flash = (hit_flash_timer * 10.0f);
                int blink = (int)flash % 2;
                player.color[0] = blink ? 3.0f : 0.5f;
                player.color[1] = blink ? 0.3f : 0.5f;
                player.color[2] = blink ? 0.3f : 0.5f;
            }
        }

        /* Update particles */
        num_particles = particles_update(particles, num_particles, delta_time);

        /* Handle resize */
        if (window_was_resized(window)) {
            window_reset_resized(window);
            renderer_handle_resize(renderer);
        }

        /* Spin enemies */
        for (int i = 0; i < num_enemies; i++) {
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
        if (num_enemies > 0) {
            renderer_draw_mesh_textured(renderer, mesh_quad, heroTexture, enemies, (u32)num_enemies);
        }
        renderer_draw_mesh(renderer, mesh_triangle, &player, 1);
        if (num_bullets > 0) {
            renderer_draw_mesh(renderer, mesh_bullet, bullets, (u32)num_bullets);
        }

        /* Particles (explosions) */
        if (num_particles > 0) {
            i32 num_p_inst = particles_to_instances(particles, num_particles,
                                                    particle_instances, MAX_PARTICLES);
            if (num_p_inst > 0) {
                renderer_draw_mesh(renderer, mesh_quad, particle_instances, (u32)num_p_inst);
            }
        }

        /* Title text */
        renderer_draw_text(renderer, "SCORE", 10.0f, 10.0f, 1.0f,
                           1.0f, 1.0f, 1.0f);

        // Render score
        {
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
    if (has_audio) audio_shutdown(audio);
    renderer_destroy(renderer);
    window_destroy(window);

    LOG_INFO("Goodbye!");
    return 0;
}
