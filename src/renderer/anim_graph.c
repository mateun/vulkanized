#include "renderer/anim_graph.h"
#include "renderer/anim_blend.h"
#include "renderer/animation.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

/* Forward declarations for blend space functions (in anim_blend_space.c) */
void blend_space_1d_evaluate(const BlendSpace1D *space, f32 param_value,
                              const SkinnedModel *model, f32 normalized_time,
                              Arena *scratch, AnimPose *out_pose);
void blend_space_2d_evaluate(const BlendSpace2D *space, f32 param_x, f32 param_y,
                              const SkinnedModel *model, f32 normalized_time,
                              Arena *scratch, AnimPose *out_pose);

/* ================================================================
 * GRAPH DEFINITION — creation, configuration, destruction
 * ================================================================ */

AnimGraphDef *anim_graph_def_create(void) {
    AnimGraphDef *def = calloc(1, sizeof(AnimGraphDef));
    return def;
}

void anim_graph_def_destroy(AnimGraphDef *def) {
    if (!def) return;

    /* Free event lists */
    for (u32 l = 0; l < def->layer_count; l++) {
        AnimLayerDef *layer = &def->layers[l];
        for (u32 s = 0; s < layer->state_count; s++) {
            AnimEventList *evts = layer->states[s].events;
            if (evts) {
                free(evts->events);
                free(evts);
            }
        }
    }

    free(def);
}

/* ---- Parameters ---- */

i32 anim_graph_def_add_param_float(AnimGraphDef *def, const char *name, f32 default_val) {
    if (def->param_count >= ANIM_MAX_PARAMS) return -1;
    u32 idx = def->param_count++;
    strncpy(def->params[idx].name, name, ANIM_PARAM_NAME_LEN - 1);
    def->params[idx].type = ANIM_PARAM_FLOAT;
    def->params[idx].default_value.f = default_val;
    return (i32)idx;
}

i32 anim_graph_def_add_param_bool(AnimGraphDef *def, const char *name, bool default_val) {
    if (def->param_count >= ANIM_MAX_PARAMS) return -1;
    u32 idx = def->param_count++;
    strncpy(def->params[idx].name, name, ANIM_PARAM_NAME_LEN - 1);
    def->params[idx].type = ANIM_PARAM_BOOL;
    def->params[idx].default_value.b = default_val;
    return (i32)idx;
}

i32 anim_graph_def_find_param(const AnimGraphDef *def, const char *name) {
    for (u32 i = 0; i < def->param_count; i++) {
        if (strcmp(def->params[i].name, name) == 0) return (i32)i;
    }
    return -1;
}

/* ---- Layers ---- */

i32 anim_graph_def_add_layer(AnimGraphDef *def, const char *name,
                              AnimLayerBlendMode blend_mode, f32 weight,
                              BoneMask *mask) {
    if (def->layer_count >= ANIM_MAX_LAYERS) return -1;
    u32 idx = def->layer_count++;
    AnimLayerDef *layer = &def->layers[idx];
    memset(layer, 0, sizeof(*layer));
    strncpy(layer->name, name, ANIM_PARAM_NAME_LEN - 1);
    layer->blend_mode = blend_mode;
    layer->weight = weight;
    layer->bone_mask = mask;
    return (i32)idx;
}

/* ---- States ---- */

i32 anim_graph_def_add_state_clip(AnimGraphDef *def, u32 layer_index,
                                   const char *name, u32 clip_index,
                                   f32 speed, bool looping) {
    if (layer_index >= def->layer_count) return -1;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (layer->state_count >= ANIM_MAX_STATES_PER_LAYER) return -1;

    u32 idx = layer->state_count++;
    AnimStateNode *state = &layer->states[idx];
    memset(state, 0, sizeof(*state));
    strncpy(state->name, name, ANIM_PARAM_NAME_LEN - 1);
    state->type = ANIM_STATE_CLIP;
    state->data.clip_index = clip_index;
    state->speed = speed;
    state->looping = looping;
    return (i32)idx;
}

i32 anim_graph_def_add_state_blend1d(AnimGraphDef *def, u32 layer_index,
                                      const char *name,
                                      const BlendSpace1DEntry *entries, u32 entry_count,
                                      u32 param_index, f32 speed, bool looping) {
    if (layer_index >= def->layer_count) return -1;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (layer->state_count >= ANIM_MAX_STATES_PER_LAYER) return -1;
    if (entry_count > ANIM_BLEND1D_MAX_CLIPS) entry_count = ANIM_BLEND1D_MAX_CLIPS;

    u32 idx = layer->state_count++;
    AnimStateNode *state = &layer->states[idx];
    memset(state, 0, sizeof(*state));
    strncpy(state->name, name, ANIM_PARAM_NAME_LEN - 1);
    state->type = ANIM_STATE_BLEND1D;
    state->speed = speed;
    state->looping = looping;

    BlendSpace1D *bs = &state->data.blend1d;
    bs->entry_count = entry_count;
    bs->param_index = param_index;
    memcpy(bs->entries, entries, sizeof(BlendSpace1DEntry) * entry_count);

    /* Sort entries by position (insertion sort — small N) */
    for (u32 i = 1; i < entry_count; i++) {
        BlendSpace1DEntry tmp = bs->entries[i];
        u32 k = i;
        while (k > 0 && bs->entries[k-1].position > tmp.position) {
            bs->entries[k] = bs->entries[k-1];
            k--;
        }
        bs->entries[k] = tmp;
    }

    return (i32)idx;
}

i32 anim_graph_def_add_state_blend2d(AnimGraphDef *def, u32 layer_index,
                                      const char *name,
                                      const BlendSpace2DEntry *entries, u32 entry_count,
                                      u32 param_x_index, u32 param_y_index,
                                      f32 speed, bool looping) {
    if (layer_index >= def->layer_count) return -1;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (layer->state_count >= ANIM_MAX_STATES_PER_LAYER) return -1;
    if (entry_count > ANIM_BLEND2D_MAX_CLIPS) entry_count = ANIM_BLEND2D_MAX_CLIPS;

    u32 idx = layer->state_count++;
    AnimStateNode *state = &layer->states[idx];
    memset(state, 0, sizeof(*state));
    strncpy(state->name, name, ANIM_PARAM_NAME_LEN - 1);
    state->type = ANIM_STATE_BLEND2D;
    state->speed = speed;
    state->looping = looping;

    BlendSpace2D *bs = &state->data.blend2d;
    bs->entry_count = entry_count;
    bs->param_x_index = param_x_index;
    bs->param_y_index = param_y_index;
    memcpy(bs->entries, entries, sizeof(BlendSpace2DEntry) * entry_count);

    return (i32)idx;
}

void anim_graph_def_set_default_state(AnimGraphDef *def, u32 layer_index, u32 state_index) {
    if (layer_index < def->layer_count)
        def->layers[layer_index].default_state = state_index;
}

/* ---- Transitions ---- */

i32 anim_graph_def_add_transition(AnimGraphDef *def, u32 layer_index,
                                   u32 source_state, u32 target_state,
                                   f32 duration) {
    if (layer_index >= def->layer_count) return -1;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (layer->transition_count >= ANIM_MAX_TRANSITIONS_PER_LAYER) return -1;

    u32 idx = layer->transition_count++;
    AnimTransition *tr = &layer->transitions[idx];
    memset(tr, 0, sizeof(*tr));
    tr->source_state = source_state;
    tr->target_state = target_state;
    tr->duration = duration;
    return (i32)idx;
}

void anim_graph_def_add_condition_float(AnimGraphDef *def, u32 layer_index,
                                         u32 transition_index,
                                         u32 param_index, AnimConditionType cmp,
                                         f32 threshold) {
    if (layer_index >= def->layer_count) return;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (transition_index >= layer->transition_count) return;
    AnimTransition *tr = &layer->transitions[transition_index];
    if (tr->condition_count >= ANIM_MAX_CONDITIONS_PER_TRANSITION) return;

    AnimCondition *c = &tr->conditions[tr->condition_count++];
    c->type = cmp;
    c->param_index = param_index;
    c->threshold = threshold;
}

void anim_graph_def_add_condition_bool(AnimGraphDef *def, u32 layer_index,
                                        u32 transition_index,
                                        u32 param_index, bool expected) {
    if (layer_index >= def->layer_count) return;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (transition_index >= layer->transition_count) return;
    AnimTransition *tr = &layer->transitions[transition_index];
    if (tr->condition_count >= ANIM_MAX_CONDITIONS_PER_TRANSITION) return;

    AnimCondition *c = &tr->conditions[tr->condition_count++];
    c->type = expected ? ANIM_COND_BOOL_TRUE : ANIM_COND_BOOL_FALSE;
    c->param_index = param_index;
}

void anim_graph_def_add_condition_callback(AnimGraphDef *def, u32 layer_index,
                                            u32 transition_index,
                                            AnimConditionCallback cb, void *user_data) {
    if (layer_index >= def->layer_count) return;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (transition_index >= layer->transition_count) return;
    AnimTransition *tr = &layer->transitions[transition_index];
    if (tr->condition_count >= ANIM_MAX_CONDITIONS_PER_TRANSITION) return;

    AnimCondition *c = &tr->conditions[tr->condition_count++];
    c->type = ANIM_COND_CALLBACK;
    c->callback = cb;
    c->callback_data = user_data;
}

void anim_graph_def_set_exit_time(AnimGraphDef *def, u32 layer_index,
                                   u32 transition_index, f32 exit_time) {
    if (layer_index >= def->layer_count) return;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (transition_index >= layer->transition_count) return;
    AnimTransition *tr = &layer->transitions[transition_index];
    tr->has_exit_time = true;
    tr->exit_time = exit_time;
}

/* ---- Events ---- */

void anim_graph_def_set_events(AnimGraphDef *def, u32 layer_index,
                                u32 state_index,
                                const AnimEvent *events, u32 event_count) {
    if (layer_index >= def->layer_count) return;
    AnimLayerDef *layer = &def->layers[layer_index];
    if (state_index >= layer->state_count) return;

    AnimStateNode *state = &layer->states[state_index];

    /* Free old events if any */
    if (state->events) {
        free(state->events->events);
        free(state->events);
    }

    AnimEventList *list = malloc(sizeof(AnimEventList));
    if (!list) return;
    list->events = malloc(sizeof(AnimEvent) * event_count);
    if (!list->events) { free(list); return; }

    memcpy(list->events, events, sizeof(AnimEvent) * event_count);
    list->event_count = event_count;

    /* Sort by time (insertion sort) */
    for (u32 i = 1; i < event_count; i++) {
        AnimEvent tmp = list->events[i];
        u32 k = i;
        while (k > 0 && list->events[k-1].time > tmp.time) {
            list->events[k] = list->events[k-1];
            k--;
        }
        list->events[k] = tmp;
    }

    state->events = list;
}

/* ================================================================
 * GRAPH INSTANCE — per-entity runtime
 * ================================================================ */

AnimGraphInstance *anim_graph_instance_create(const AnimGraphDef *def,
                                              const SkinnedModel *model) {
    AnimGraphInstance *inst = calloc(1, sizeof(AnimGraphInstance));
    if (!inst) return NULL;

    inst->def = def;
    inst->joint_count = model->skeleton.joint_count;

    /* Initialize parameters with defaults */
    for (u32 i = 0; i < def->param_count; i++) {
        if (def->params[i].type == ANIM_PARAM_FLOAT)
            inst->params.values[i].f = def->params[i].default_value.f;
        else
            inst->params.values[i].b = def->params[i].default_value.b;
    }

    /* Initialize layer states */
    for (u32 l = 0; l < def->layer_count; l++) {
        AnimLayerState *ls = &inst->layer_states[l];
        ls->current_state = def->layers[l].default_state;
        ls->state_time = 0.0f;
    }

    /* Identity joint matrices */
    for (u32 j = 0; j < inst->joint_count; j++) {
        memset(inst->joint_matrices[j], 0, sizeof(f32) * 16);
        inst->joint_matrices[j][0]  = 1.0f;
        inst->joint_matrices[j][5]  = 1.0f;
        inst->joint_matrices[j][10] = 1.0f;
        inst->joint_matrices[j][15] = 1.0f;
    }

    return inst;
}

void anim_graph_instance_destroy(AnimGraphInstance *instance) {
    free(instance);
}

/* ---- Parameter control ---- */

void anim_graph_set_param_float(AnimGraphInstance *inst, u32 param_index, f32 value) {
    if (param_index < ANIM_MAX_PARAMS)
        inst->params.values[param_index].f = value;
}

void anim_graph_set_param_bool(AnimGraphInstance *inst, u32 param_index, bool value) {
    if (param_index < ANIM_MAX_PARAMS)
        inst->params.values[param_index].b = value;
}

void anim_graph_set_param_float_by_name(AnimGraphInstance *inst, const char *name, f32 value) {
    i32 idx = anim_graph_def_find_param(inst->def, name);
    if (idx >= 0) inst->params.values[idx].f = value;
}

void anim_graph_set_param_bool_by_name(AnimGraphInstance *inst, const char *name, bool value) {
    i32 idx = anim_graph_def_find_param(inst->def, name);
    if (idx >= 0) inst->params.values[idx].b = value;
}

void anim_graph_set_event_callback(AnimGraphInstance *inst,
                                    AnimEventCallback callback, void *user_data) {
    inst->event_callback = callback;
    inst->event_user_data = user_data;
}

/* ================================================================
 * INTERNAL: condition evaluation
 * ================================================================ */

static bool evaluate_condition(const AnimCondition *cond, const AnimParamValues *params) {
    switch (cond->type) {
    case ANIM_COND_FLOAT_GT: return params->values[cond->param_index].f >  cond->threshold;
    case ANIM_COND_FLOAT_LT: return params->values[cond->param_index].f <  cond->threshold;
    case ANIM_COND_FLOAT_GE: return params->values[cond->param_index].f >= cond->threshold;
    case ANIM_COND_FLOAT_LE: return params->values[cond->param_index].f <= cond->threshold;
    case ANIM_COND_BOOL_TRUE:  return params->values[cond->param_index].b == true;
    case ANIM_COND_BOOL_FALSE: return params->values[cond->param_index].b == false;
    case ANIM_COND_CALLBACK:
        return cond->callback ? cond->callback(cond->callback_data, params) : false;
    }
    return false;
}

static bool evaluate_transition(const AnimTransition *tr, const AnimParamValues *params,
                                 f32 state_normalized_time) {
    /* Check exit time first */
    if (tr->has_exit_time && state_normalized_time < tr->exit_time)
        return false;

    /* All conditions must pass (AND logic) */
    for (u32 i = 0; i < tr->condition_count; i++) {
        if (!evaluate_condition(&tr->conditions[i], params))
            return false;
    }
    return tr->condition_count > 0; /* Must have at least one condition */
}

/* ================================================================
 * INTERNAL: get clip duration for a state (used for time wrapping)
 * ================================================================ */

static f32 get_state_duration(const AnimStateNode *state, const SkinnedModel *model,
                               const AnimParamValues *params) {
    switch (state->type) {
    case ANIM_STATE_CLIP:
        if (state->data.clip_index < model->clip_count)
            return model->clips[state->data.clip_index].duration;
        return 1.0f;
    case ANIM_STATE_BLEND1D: {
        /* Use weighted average duration of the two bracketing clips */
        const BlendSpace1D *bs = &state->data.blend1d;
        if (bs->entry_count == 0) return 1.0f;
        f32 p = params->values[bs->param_index].f;
        f32 lo_pos = bs->entries[0].position;
        f32 hi_pos = bs->entries[bs->entry_count - 1].position;
        if (p <= lo_pos) p = lo_pos;
        if (p >= hi_pos) p = hi_pos;

        if (bs->entry_count == 1) {
            u32 ci = bs->entries[0].clip_index;
            return (ci < model->clip_count) ? model->clips[ci].duration : 1.0f;
        }

        /* Find bracketing */
        u32 lo = 0, hi = 1;
        for (u32 i = 1; i < bs->entry_count; i++) {
            if (bs->entries[i].position >= p) { hi = i; lo = i - 1; break; }
        }
        f32 range = bs->entries[hi].position - bs->entries[lo].position;
        f32 factor = (range > 1e-6f) ? (p - bs->entries[lo].position) / range : 0.0f;

        f32 dur_a = (bs->entries[lo].clip_index < model->clip_count)
            ? model->clips[bs->entries[lo].clip_index].duration : 1.0f;
        f32 dur_b = (bs->entries[hi].clip_index < model->clip_count)
            ? model->clips[bs->entries[hi].clip_index].duration : 1.0f;
        return dur_a * (1.0f - factor) + dur_b * factor;
    }
    case ANIM_STATE_BLEND2D:
        /* Approximate: use first entry's duration */
        if (state->data.blend2d.entry_count > 0) {
            u32 ci = state->data.blend2d.entries[0].clip_index;
            return (ci < model->clip_count) ? model->clips[ci].duration : 1.0f;
        }
        return 1.0f;
    }
    return 1.0f;
}

/* ================================================================
 * INTERNAL: evaluate a state into an AnimPose
 * ================================================================ */

static void evaluate_state(const AnimStateNode *state, const SkinnedModel *model,
                            const AnimParamValues *params,
                            f32 state_time, Arena *scratch, AnimPose *out_pose) {
    const Skeleton *skel = &model->skeleton;

    switch (state->type) {
    case ANIM_STATE_CLIP: {
        u32 ci = state->data.clip_index;
        if (ci < model->clip_count) {
            animation_evaluate_pose(skel, &model->clips[ci], state_time, out_pose);
        } else {
            pose_from_rest(skel, out_pose);
        }
        break;
    }
    case ANIM_STATE_BLEND1D: {
        f32 dur = get_state_duration(state, model, params);
        f32 norm_t = (dur > 1e-6f) ? state_time / dur : 0.0f;
        blend_space_1d_evaluate(&state->data.blend1d,
                                 params->values[state->data.blend1d.param_index].f,
                                 model, norm_t, scratch, out_pose);
        break;
    }
    case ANIM_STATE_BLEND2D: {
        f32 dur = get_state_duration(state, model, params);
        f32 norm_t = (dur > 1e-6f) ? state_time / dur : 0.0f;
        blend_space_2d_evaluate(&state->data.blend2d,
                                 params->values[state->data.blend2d.param_x_index].f,
                                 params->values[state->data.blend2d.param_y_index].f,
                                 model, norm_t, scratch, out_pose);
        break;
    }
    }
}

/* ================================================================
 * INTERNAL: fire events that were crossed between prev_time and curr_time
 * ================================================================ */

static void fire_events(const AnimEventList *events, f32 prev_time, f32 curr_time,
                         f32 duration, bool looping,
                         AnimEventCallback callback, void *user_data) {
    if (!events || !callback || events->event_count == 0) return;
    (void)duration;  /* duration implicitly bounded by curr_time wrap */

    if (!looping || curr_time >= prev_time) {
        /* Normal case: time increased without wrapping */
        for (u32 i = 0; i < events->event_count; i++) {
            f32 et = events->events[i].time;
            if (et > prev_time && et <= curr_time) {
                callback(user_data, events->events[i].event_id,
                         events->events[i].name);
            }
        }
    } else {
        /* Looping wrap: fire events from prev_time..duration and 0..curr_time */
        for (u32 i = 0; i < events->event_count; i++) {
            f32 et = events->events[i].time;
            if (et > prev_time || et <= curr_time) {
                callback(user_data, events->events[i].event_id,
                         events->events[i].name);
            }
        }
    }
}

/* ================================================================
 * anim_graph_update — the main per-frame entry point
 * ================================================================ */

void anim_graph_update(AnimGraphInstance *inst, const SkinnedModel *model,
                        f32 delta_time, Arena *scratch) {
    const AnimGraphDef *def = inst->def;
    const Skeleton *skel = &model->skeleton;
    u32 jc = skel->joint_count;

    /* We need one pose per layer, plus a final composite pose */
    AnimPose *layer_poses[ANIM_MAX_LAYERS];
    for (u32 l = 0; l < def->layer_count; l++) {
        layer_poses[l] = arena_push(scratch, AnimPose);
        if (!layer_poses[l]) {
            LOG_ERROR("anim_graph_update: scratch arena out of memory");
            return;
        }
    }

    /* Evaluate each layer */
    for (u32 l = 0; l < def->layer_count; l++) {
        const AnimLayerDef *layer_def = &def->layers[l];
        AnimLayerState *ls = &inst->layer_states[l];

        if (layer_def->state_count == 0) {
            pose_from_rest(skel, layer_poses[l]);
            continue;
        }

        const AnimStateNode *cur_state = &layer_def->states[ls->current_state];
        f32 cur_duration = get_state_duration(cur_state, model, &inst->params);

        /* 1. Check transitions (only if not already transitioning) */
        if (!ls->transitioning) {
            f32 norm_time = (cur_duration > 1e-6f) ? ls->state_time / cur_duration : 0.0f;

            for (u32 t = 0; t < layer_def->transition_count; t++) {
                const AnimTransition *tr = &layer_def->transitions[t];
                if (tr->source_state != ls->current_state) continue;

                if (evaluate_transition(tr, &inst->params, norm_time)) {
                    ls->transitioning = true;
                    ls->prev_state = ls->current_state;
                    ls->prev_state_time = ls->state_time;
                    ls->transition_elapsed = 0.0f;
                    ls->transition_duration = tr->duration;
                    ls->current_state = tr->target_state;
                    ls->state_time = 0.0f;
                    cur_state = &layer_def->states[ls->current_state];
                    cur_duration = get_state_duration(cur_state, model, &inst->params);
                    break;
                }
            }
        }

        /* 2. Advance time */
        f32 prev_time = ls->state_time;
        ls->state_time += delta_time * cur_state->speed;

        if (cur_state->looping && cur_duration > 0.0f) {
            ls->state_time = fmodf(ls->state_time, cur_duration);
            if (ls->state_time < 0.0f) ls->state_time += cur_duration;
        } else {
            if (ls->state_time > cur_duration) ls->state_time = cur_duration;
        }
        ls->state_normalized = (cur_duration > 1e-6f) ? ls->state_time / cur_duration : 0.0f;

        /* 3. Evaluate current state */
        AnimPose *cur_pose = arena_push(scratch, AnimPose);
        if (!cur_pose) { pose_from_rest(skel, layer_poses[l]); continue; }
        evaluate_state(cur_state, model, &inst->params, ls->state_time, scratch, cur_pose);

        /* 4. If transitioning, evaluate previous state and blend */
        if (ls->transitioning) {
            const AnimStateNode *prev_state = &layer_def->states[ls->prev_state];
            f32 prev_dur = get_state_duration(prev_state, model, &inst->params);

            /* Advance prev state time too */
            ls->prev_state_time += delta_time * prev_state->speed;
            if (prev_state->looping && prev_dur > 0.0f) {
                ls->prev_state_time = fmodf(ls->prev_state_time, prev_dur);
                if (ls->prev_state_time < 0.0f) ls->prev_state_time += prev_dur;
            } else {
                if (ls->prev_state_time > prev_dur) ls->prev_state_time = prev_dur;
            }

            AnimPose *prev_pose = arena_push(scratch, AnimPose);
            if (prev_pose) {
                evaluate_state(prev_state, model, &inst->params,
                               ls->prev_state_time, scratch, prev_pose);

                ls->transition_elapsed += delta_time;
                f32 blend_factor = (ls->transition_duration > 1e-6f)
                    ? ls->transition_elapsed / ls->transition_duration : 1.0f;

                if (blend_factor >= 1.0f) {
                    /* Transition complete */
                    blend_factor = 1.0f;
                    ls->transitioning = false;
                }

                pose_blend(prev_pose, cur_pose, jc, blend_factor, layer_poses[l]);
            } else {
                pose_copy(cur_pose, jc, layer_poses[l]);
            }
        } else {
            pose_copy(cur_pose, jc, layer_poses[l]);
        }

        /* 5. Fire events */
        if (cur_state->events && inst->event_callback) {
            fire_events(cur_state->events, prev_time, ls->state_time,
                         cur_duration, cur_state->looping,
                         inst->event_callback, inst->event_user_data);
        }
        ls->prev_event_time = ls->state_time;
    }

    /* Composite layers */
    if (def->layer_count == 0) {
        /* No layers: rest pose */
        AnimPose rest;
        pose_from_rest(skel, &rest);
        animation_pose_to_matrices(&rest, skel, inst->joint_matrices, scratch);
        inst->joint_count = jc;
        return;
    }

    AnimPose *final_pose = layer_poses[0];

    for (u32 l = 1; l < def->layer_count; l++) {
        const AnimLayerDef *layer_def = &def->layers[l];
        AnimPose *composite = arena_push(scratch, AnimPose);
        if (!composite) break;

        if (layer_def->blend_mode == ANIM_LAYER_OVERRIDE) {
            if (layer_def->bone_mask) {
                pose_blend_masked(final_pose, layer_poses[l], jc,
                                   layer_def->bone_mask, layer_def->weight, composite);
            } else {
                pose_blend(final_pose, layer_poses[l], jc, layer_def->weight, composite);
            }
        } else { /* ANIM_LAYER_ADDITIVE */
            AnimPose *ref = arena_push(scratch, AnimPose);
            if (ref) {
                pose_from_rest(skel, ref);
                pose_blend_additive(final_pose, layer_poses[l], ref, jc,
                                     layer_def->bone_mask, layer_def->weight, composite);
            } else {
                pose_copy(final_pose, jc, composite);
            }
        }

        final_pose = composite;
    }

    /* Convert final pose to skinning matrices */
    animation_pose_to_matrices(final_pose, skel, inst->joint_matrices, scratch);
    inst->joint_count = jc;
}
