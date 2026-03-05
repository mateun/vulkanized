#include "core/common.h"
#include "core/log.h"
#include "core/arena.h"
#include "platform/window.h"
#include "platform/input.h"
#include "renderer/renderer.h"
#include "renderer/animation.h"
#include "renderer/anim_graph.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Event callback ---- */
static void on_anim_event(void *user_data, u32 event_id, const char *name) {
    (void)user_data;
    LOG_INFO("Animation event: id=%u name=\"%s\"", event_id, name);
}

int main(void) {
#ifdef ENGINE_DEBUG
    log_init(LOG_LEVEL_TRACE);
#else
    log_init(LOG_LEVEL_INFO);
#endif

    LOG_INFO("Animation Graph Demo starting...");

    /* ---- Create window ---- */
    WindowConfig win_config = {
        .title     = "Animation Graph Demo",
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

    /* ---- Load animated model ---- */
    SkinnedModel model = {0};
    bool has_model = false;

    if (renderer_load_skinned_model_file(renderer, "assets/cesiumman.glb", &model) == ENGINE_SUCCESS) {
        has_model = true;
        LOG_INFO("Loaded skinned model: %u joints, %u clips",
                 model.skeleton.joint_count, model.clip_count);
        for (u32 i = 0; i < model.clip_count; i++) {
            LOG_INFO("  Clip %u: \"%s\" (%.2f s)", i, model.clips[i].name,
                     model.clips[i].duration);
        }
    } else {
        LOG_WARN("Failed to load skinned model — demo will show empty scene");
        LOG_WARN("Place a skinned .glb file at assets/cesiumman.glb");
    }

    /* ==================================================================
     * BUILD ANIMATION GRAPH
     * ==================================================================
     * Even with a single clip, this demonstrates the full AnimGraph API.
     * With multiple clips, you'd add blend spaces and state transitions.
     * ================================================================== */

    AnimGraphDef      *graph_def  = NULL;
    AnimGraphInstance  *graph_inst = NULL;
    i32                p_clip     = -1;

    if (has_model) {
        graph_def = anim_graph_def_create();

        /* Base layer — override mode, no bone mask */
        i32 base_layer = anim_graph_def_add_layer(graph_def, "base",
                                                   ANIM_LAYER_OVERRIDE, 1.0f, NULL);

        if (model.clip_count == 1) {
            /* Single clip: one looping state */
            i32 s_anim = anim_graph_def_add_state_clip(
                graph_def, (u32)base_layer, "animation",
                0, 1.0f, true);
            anim_graph_def_set_default_state(graph_def, (u32)base_layer, (u32)s_anim);
        } else if (model.clip_count >= 2) {
            /* Multiple clips: create states and transitions for each.
             * Use the "clip" parameter to drive transitions between states. */
            p_clip = anim_graph_def_add_param_float(graph_def, "clip", 0.0f);

            for (u32 i = 0; i < model.clip_count && i < ANIM_MAX_STATES_PER_LAYER; i++) {
                char state_name[32];
                snprintf(state_name, sizeof(state_name), "clip_%u", i);
                anim_graph_def_add_state_clip(
                    graph_def, (u32)base_layer, state_name,
                    i, 1.0f, true);
            }
            anim_graph_def_set_default_state(graph_def, (u32)base_layer, 0);

            /* Add crossfade transitions between all clip pairs */
            for (u32 src = 0; src < model.clip_count && src < ANIM_MAX_STATES_PER_LAYER; src++) {
                for (u32 dst = 0; dst < model.clip_count && dst < ANIM_MAX_STATES_PER_LAYER; dst++) {
                    if (src == dst) continue;
                    i32 tr = anim_graph_def_add_transition(
                        graph_def, (u32)base_layer, src, dst, 0.2f);
                    if (tr >= 0) {
                        /* Transition when "clip" parameter matches destination index */
                        anim_graph_def_add_condition_float(
                            graph_def, (u32)base_layer, (u32)tr,
                            (u32)p_clip, ANIM_COND_FLOAT_GE, (f32)dst - 0.1f);
                        anim_graph_def_add_condition_float(
                            graph_def, (u32)base_layer, (u32)tr,
                            (u32)p_clip, ANIM_COND_FLOAT_LE, (f32)dst + 0.1f);
                    }
                }
            }
        }

        /* Create instance */
        graph_inst = anim_graph_instance_create(graph_def, &model);
        anim_graph_set_event_callback(graph_inst, on_anim_event, NULL);

        LOG_INFO("Animation graph created: %u layers, %u params",
                 graph_def->layer_count, graph_def->param_count);
    }

    /* ---- Scratch arena for per-frame graph evaluation ---- */
    #define SCRATCH_SIZE (64 * 1024)  /* 64 KB */
    u8 scratch_buf[SCRATCH_SIZE];
    Arena scratch;
    arena_init(&scratch, scratch_buf, SCRATCH_SIZE);

    /* ---- Frame timing ---- */
    f64 last_time = glfwGetTime();
    f32 total_time = 0.0f;
    f32 anim_speed = 1.0f;

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

        /* Speed control: UP/DOWN arrows */
        if (input_key_pressed(GLFW_KEY_UP)) {
            anim_speed += 0.25f;
            if (anim_speed > 3.0f) anim_speed = 3.0f;
            LOG_INFO("Animation speed: %.2fx", anim_speed);
        }
        if (input_key_pressed(GLFW_KEY_DOWN)) {
            anim_speed -= 0.25f;
            if (anim_speed < 0.0f) anim_speed = 0.0f;
            LOG_INFO("Animation speed: %.2fx", anim_speed);
        }

        /* Switch clips with number keys (for multi-clip models) */
        if (has_model && model.clip_count > 1 && p_clip >= 0) {
            for (u32 i = 0; i < model.clip_count && i < 9; i++) {
                if (input_key_pressed(GLFW_KEY_1 + (int)i)) {
                    anim_graph_set_param_float(graph_inst, (u32)p_clip, (f32)i);
                    LOG_INFO("Switched to clip %u: \"%s\"", i, model.clips[i].name);
                }
            }
        }

        /* ---- Update animation graph ---- */
        if (has_model && graph_inst) {
            arena_reset(&scratch);
            anim_graph_update(graph_inst, &model, dt * anim_speed, &scratch);
        }

        /* ---- Camera: orbit around origin ---- */
        f32 cam_dist   = 4.0f;
        f32 cam_angle  = total_time * 0.3f;
        f32 cam_height = 1.5f;

        Camera3D camera = {
            .position   = { sinf(cam_angle) * cam_dist, cam_height, cosf(cam_angle) * cam_dist },
            .target     = { 0.0f, 0.8f, 0.0f },
            .up         = { 0.0f, 1.0f, 0.0f },
            .fov        = 60.0f,
            .near_plane = 0.1f,
            .far_plane  = 100.0f,
        };

        /* ---- Directional light ---- */
        DirectionalLight light = {
            .direction = { 0.3f, -1.0f, 0.5f },
            .color     = { 1.0f,  0.95f, 0.9f },
            .ambient   = { 0.2f, 0.2f, 0.25f },
            .shininess = 32.0f,
        };

        /* ---- Instance data for the model ---- */
        InstanceData3D model_inst = {
            .position = { 0.0f, 0.0f, 0.0f },
            .rotation = { 0.0f, 0.0f, 0.0f },
            .scale    = { 1.0f, 1.0f, 1.0f },
            .color    = { 1.0f, 1.0f, 1.0f },
        };

        /* ---- Render ---- */
        if (renderer_begin_frame(renderer) != ENGINE_SUCCESS) continue;

        renderer_set_camera_3d(renderer, &camera);
        renderer_set_light(renderer, &light);

        if (has_model && graph_inst) {
            renderer_draw_skinned(renderer, model.mesh_handle, &model_inst,
                                  (const f32 (*)[16])graph_inst->joint_matrices,
                                  graph_inst->joint_count);
        }

        /* ---- Text overlay ---- */
        char fps_str[64];
        snprintf(fps_str, sizeof(fps_str), "dt: %.2f ms", dt * 1000.0f);
        renderer_draw_text(renderer, "ANIMATION GRAPH DEMO", 10, 10, 1.0f, 1, 1, 1);
        renderer_draw_text(renderer, fps_str, 10, 40, 0.7f, 0, 1, 0);

        if (has_model && graph_inst) {
            /* Current state info */
            const AnimLayerDef *layer_def = &graph_def->layers[0];
            const AnimLayerState *layer_st = &graph_inst->layer_states[0];
            const AnimStateNode *cur_state = &layer_def->states[layer_st->current_state];

            char state_str[128];
            snprintf(state_str, sizeof(state_str), "State: \"%s\" (%.1f s)",
                     cur_state->name, layer_st->state_time);
            renderer_draw_text(renderer, state_str, 10, 65, 0.6f, 0.7f, 0.7f, 1.0f);

            /* Speed */
            char speed_str[64];
            snprintf(speed_str, sizeof(speed_str), "Speed: %.2fx (UP/DOWN to change)", anim_speed);
            renderer_draw_text(renderer, speed_str, 10, 85, 0.6f, 0.5f, 1.0f, 0.5f);

            /* Joint count */
            char joint_str[64];
            snprintf(joint_str, sizeof(joint_str), "Joints: %u", model.skeleton.joint_count);
            renderer_draw_text(renderer, joint_str, 10, 105, 0.6f, 0.5f, 0.5f, 0.5f);

            /* Transition indicator */
            if (layer_st->transitioning) {
                char trans_str[128];
                snprintf(trans_str, sizeof(trans_str), "TRANSITIONING: %.0f%%",
                         (layer_st->transition_elapsed / layer_st->transition_duration) * 100.0f);
                renderer_draw_text(renderer, trans_str, 10, 125, 0.6f, 1.0f, 0.7f, 0.3f);
            }
        }

        /* Controls */
        if (has_model && model.clip_count > 1) {
            renderer_draw_text(renderer, "ESC quit | UP/DOWN speed | 1-9 clips",
                               10, 145, 0.5f, 0.5f, 0.5f, 0.5f);
        } else {
            renderer_draw_text(renderer, "ESC quit | UP/DOWN speed",
                               10, 145, 0.5f, 0.5f, 0.5f, 0.5f);
        }

        renderer_end_frame(renderer);

        input_update();
    }

    /* ---- Cleanup ---- */
    if (graph_inst) anim_graph_instance_destroy(graph_inst);
    if (graph_def)  anim_graph_def_destroy(graph_def);
    if (has_model)  skinned_model_destroy(&model);
    renderer_destroy(renderer);
    window_destroy(window);

    LOG_INFO("Animation Graph Demo finished");
    return 0;
}
