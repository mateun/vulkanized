#ifndef ENGINE_ANIM_GRAPH_H
#define ENGINE_ANIM_GRAPH_H

#include "core/common.h"
#include "core/arena.h"
#include "renderer/anim_graph_types.h"
#include "renderer/animation_types.h"

/* ================================================================
 * GRAPH DEFINITION — build a graph description (shared, static)
 * ================================================================ */

AnimGraphDef *anim_graph_def_create(void);
void          anim_graph_def_destroy(AnimGraphDef *def);

/* ---- Parameters ---- */
i32 anim_graph_def_add_param_float(AnimGraphDef *def, const char *name, f32 default_val);
i32 anim_graph_def_add_param_bool(AnimGraphDef *def, const char *name, bool default_val);
i32 anim_graph_def_find_param(const AnimGraphDef *def, const char *name);

/* ---- Layers ---- */
i32 anim_graph_def_add_layer(AnimGraphDef *def, const char *name,
                              AnimLayerBlendMode blend_mode, f32 weight,
                              BoneMask *mask);

/* ---- States ---- */
i32 anim_graph_def_add_state_clip(AnimGraphDef *def, u32 layer_index,
                                   const char *name, u32 clip_index,
                                   f32 speed, bool looping);

i32 anim_graph_def_add_state_blend1d(AnimGraphDef *def, u32 layer_index,
                                      const char *name,
                                      const BlendSpace1DEntry *entries, u32 entry_count,
                                      u32 param_index, f32 speed, bool looping);

i32 anim_graph_def_add_state_blend2d(AnimGraphDef *def, u32 layer_index,
                                      const char *name,
                                      const BlendSpace2DEntry *entries, u32 entry_count,
                                      u32 param_x_index, u32 param_y_index,
                                      f32 speed, bool looping);

void anim_graph_def_set_default_state(AnimGraphDef *def, u32 layer_index, u32 state_index);

/* ---- Transitions ---- */
i32 anim_graph_def_add_transition(AnimGraphDef *def, u32 layer_index,
                                   u32 source_state, u32 target_state,
                                   f32 duration);

void anim_graph_def_add_condition_float(AnimGraphDef *def, u32 layer_index,
                                         u32 transition_index,
                                         u32 param_index, AnimConditionType cmp,
                                         f32 threshold);

void anim_graph_def_add_condition_bool(AnimGraphDef *def, u32 layer_index,
                                        u32 transition_index,
                                        u32 param_index, bool expected);

void anim_graph_def_add_condition_callback(AnimGraphDef *def, u32 layer_index,
                                            u32 transition_index,
                                            AnimConditionCallback cb, void *user_data);

void anim_graph_def_set_exit_time(AnimGraphDef *def, u32 layer_index,
                                   u32 transition_index, f32 exit_time);

/* ---- Events ---- */
void anim_graph_def_set_events(AnimGraphDef *def, u32 layer_index,
                                u32 state_index,
                                const AnimEvent *events, u32 event_count);

/* ---- Bone Masks ---- */
BoneMask *bone_mask_create_from_joint(const Skeleton *skeleton,
                                       u32 root_joint_index, f32 weight);
BoneMask *bone_mask_create_excluding_joint(const Skeleton *skeleton,
                                            u32 exclude_root_index);
void bone_mask_destroy(BoneMask *mask);

/* ================================================================
 * GRAPH INSTANCE — per-entity runtime (create, update, destroy)
 * ================================================================ */

AnimGraphInstance *anim_graph_instance_create(const AnimGraphDef *def,
                                              const SkinnedModel *model);
void anim_graph_instance_destroy(AnimGraphInstance *instance);

/* ---- Parameter control ---- */
void anim_graph_set_param_float(AnimGraphInstance *inst, u32 param_index, f32 value);
void anim_graph_set_param_bool(AnimGraphInstance *inst, u32 param_index, bool value);
void anim_graph_set_param_float_by_name(AnimGraphInstance *inst, const char *name, f32 value);
void anim_graph_set_param_bool_by_name(AnimGraphInstance *inst, const char *name, bool value);

/* ---- Event callback ---- */
void anim_graph_set_event_callback(AnimGraphInstance *inst,
                                    AnimEventCallback callback, void *user_data);

/* ---- Update (the main entry point each frame) ---- */
void anim_graph_update(AnimGraphInstance *inst, const SkinnedModel *model,
                        f32 delta_time, Arena *scratch);

#endif /* ENGINE_ANIM_GRAPH_H */
