#ifndef ENGINE_ANIMATION_H
#define ENGINE_ANIMATION_H

#include "core/common.h"
#include "core/arena.h"
#include "renderer/animation_types.h"

/* Initialize an AnimState for a given skinned model.
 * Sets current_clip=0, time=0, looping=true, speed=1.0. */
void animation_state_init(AnimState *state, const SkinnedModel *model);

/* Advance animation time and recompute joint matrices.
 * delta_time: seconds since last frame.
 * model: the skinned model (skeleton + clips).
 * state: in/out — updated time + joint_matrices output. */
void animation_update(AnimState *state, const SkinnedModel *model, f32 delta_time);

/* Sample a specific animation clip at a given time, writing joint matrices.
 * Lower-level than animation_update — for manual control or blending. */
void animation_sample(const SkinnedModel *model, u32 clip_index, f32 time,
                      f32 out_joint_matrices[][16]);

/* Blend two poses by factor (0.0 = pose_a, 1.0 = pose_b).
 * Per-element matrix lerp. */
void animation_blend(const f32 pose_a[][16], const f32 pose_b[][16],
                     u32 joint_count, f32 factor,
                     f32 out_joint_matrices[][16]);

/* Evaluate a clip into a local-space AnimPose (T, R, S per joint).
 * Building block for the animation graph system. */
void animation_evaluate_pose(const Skeleton *skel, const AnimClip *clip,
                              f32 time, AnimPose *out_pose);

/* Convert a local-space AnimPose to final joint skinning matrices.
 * Builds T*R*S -> parent chain -> inverse bind.
 * scratch: arena for temporary mat4 arrays (needs ~16KB). */
void animation_pose_to_matrices(const AnimPose *pose, const Skeleton *skel,
                                 f32 out_joint_matrices[][16], Arena *scratch);

/* Destroy a skinned model's animation data (clips, channels).
 * Does NOT destroy the mesh handle (renderer owns that). */
void skinned_model_destroy(SkinnedModel *model);

#endif /* ENGINE_ANIMATION_H */
