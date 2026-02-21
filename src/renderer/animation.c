#include "renderer/animation.h"
#include "core/log.h"

#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/vec3.h>
#include <cglm/affine.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Binary search: find the last keyframe index whose timestamp <= time.
 * Returns the lower keyframe of the pair bracketing `time`.
 * ------------------------------------------------------------------------ */

static u32 find_keyframe(const f32 *timestamps, u32 count, f32 time) {
    u32 lo = 0, hi = count - 1;
    while (lo < hi - 1) {
        u32 mid = (lo + hi) / 2;
        if (timestamps[mid] <= time)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

/* --------------------------------------------------------------------------
 * Sample a single animation channel at the given time.
 * Writes the interpolated value to `out`.
 * For TRANSLATION/SCALE: 3 floats. For ROTATION: 4 floats (quaternion xyzw).
 * ------------------------------------------------------------------------ */

static void sample_channel(const AnimChannel *ch, f32 time, f32 *out) {
    u32 components = (ch->path == ANIM_PATH_ROTATION) ? 4 : 3;

    if (ch->keyframe_count == 0) return;

    /* Single keyframe or before first: snap to first value */
    if (ch->keyframe_count == 1 || time <= ch->timestamps[0]) {
        if (ch->interpolation == ANIM_INTERP_CUBICSPLINE) {
            /* CubicSpline: layout is [in_tangent, value, out_tangent] per keyframe */
            memcpy(out, ch->values + components, sizeof(f32) * components);
        } else {
            memcpy(out, ch->values, sizeof(f32) * components);
        }
        return;
    }

    /* After last keyframe: snap to last value */
    if (time >= ch->timestamps[ch->keyframe_count - 1]) {
        u32 last = ch->keyframe_count - 1;
        if (ch->interpolation == ANIM_INTERP_CUBICSPLINE) {
            memcpy(out, ch->values + last * 3 * components + components,
                   sizeof(f32) * components);
        } else {
            memcpy(out, ch->values + last * components, sizeof(f32) * components);
        }
        return;
    }

    /* Find bracketing keyframes */
    u32 k0 = find_keyframe(ch->timestamps, ch->keyframe_count, time);
    u32 k1 = k0 + 1;
    f32 t0 = ch->timestamps[k0];
    f32 t1 = ch->timestamps[k1];
    f32 t  = (t1 - t0 > 1e-6f) ? (time - t0) / (t1 - t0) : 0.0f;

    switch (ch->interpolation) {
    case ANIM_INTERP_STEP:
        if (ch->interpolation == ANIM_INTERP_CUBICSPLINE) {
            memcpy(out, ch->values + k0 * 3 * components + components,
                   sizeof(f32) * components);
        } else {
            memcpy(out, ch->values + k0 * components, sizeof(f32) * components);
        }
        break;

    case ANIM_INTERP_LINEAR:
        if (ch->path == ANIM_PATH_ROTATION) {
            glm_quat_slerp(
                ch->values + k0 * 4,
                ch->values + k1 * 4,
                t, out);
        } else {
            glm_vec3_lerp(
                ch->values + k0 * 3,
                ch->values + k1 * 3,
                t, out);
        }
        break;

    case ANIM_INTERP_CUBICSPLINE: {
        /* Cubic Hermite spline:
         * p(t) = (2t^3 - 3t^2 + 1)*v0 + (t^3 - 2t^2 + t)*b0*dt
         *      + (-2t^3 + 3t^2)*v1 + (t^3 - t^2)*a1*dt
         *
         * Layout per keyframe: [in_tangent (components), value (components), out_tangent (components)]
         */
        f32 dt = t1 - t0;
        f32 t2 = t * t;
        f32 t3 = t2 * t;

        const f32 *v0 = ch->values + k0 * 3 * components + components;         /* value at k0 */
        const f32 *b0 = ch->values + k0 * 3 * components + 2 * components;     /* out-tangent at k0 */
        const f32 *v1 = ch->values + k1 * 3 * components + components;         /* value at k1 */
        const f32 *a1 = ch->values + k1 * 3 * components;                      /* in-tangent at k1 */

        for (u32 i = 0; i < components; i++) {
            out[i] = (2.0f*t3 - 3.0f*t2 + 1.0f) * v0[i]
                   + (t3 - 2.0f*t2 + t) * dt * b0[i]
                   + (-2.0f*t3 + 3.0f*t2) * v1[i]
                   + (t3 - t2) * dt * a1[i];
        }

        if (ch->path == ANIM_PATH_ROTATION) {
            glm_quat_normalize(out);
        }
        break;
    }
    }
}

/* --------------------------------------------------------------------------
 * animation_evaluate_pose — public function.
 * Evaluate all channels in a clip at time t to produce per-joint local TRS.
 * Starts from rest pose and overrides with animated channels.
 * ------------------------------------------------------------------------ */

void animation_evaluate_pose(const Skeleton *skel, const AnimClip *clip,
                              f32 time, AnimPose *out_pose) {
    /* Start from rest pose */
    for (u32 j = 0; j < skel->joint_count; j++) {
        memcpy(out_pose->translations[j], skel->rest_translations[j], sizeof(f32) * 3);
        memcpy(out_pose->rotations[j],    skel->rest_rotations[j],    sizeof(f32) * 4);
        memcpy(out_pose->scales[j],       skel->rest_scales[j],       sizeof(f32) * 3);
    }

    /* Override with animated channels */
    for (u32 c = 0; c < clip->channel_count; c++) {
        const AnimChannel *ch = &clip->channels[c];
        u32 j = ch->target_joint;
        if (j >= skel->joint_count) continue;

        switch (ch->path) {
        case ANIM_PATH_TRANSLATION:
            sample_channel(ch, time, out_pose->translations[j]);
            break;
        case ANIM_PATH_ROTATION:
            sample_channel(ch, time, out_pose->rotations[j]);
            break;
        case ANIM_PATH_SCALE:
            sample_channel(ch, time, out_pose->scales[j]);
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 * animation_pose_to_matrices — public function.
 * Convert a local-space AnimPose to final joint skinning matrices.
 * Builds T*R*S -> parent chain -> inverse bind.
 * Uses scratch arena for temporary mat4 arrays.
 * ------------------------------------------------------------------------ */

void animation_pose_to_matrices(const AnimPose *pose, const Skeleton *skel,
                                 f32 out_joint_matrices[][16], Arena *scratch) {
    u32 jc = skel->joint_count;

    /* Allocate temp arrays from arena */
    mat4 *local_transforms  = arena_push_array(scratch, mat4, jc);
    mat4 *global_transforms = arena_push_array(scratch, mat4, jc);

    if (!local_transforms || !global_transforms) {
        LOG_ERROR("animation_pose_to_matrices: scratch arena out of memory");
        for (u32 j = 0; j < jc; j++)
            glm_mat4_identity((vec4 *)out_joint_matrices[j]);
        return;
    }

    /* Build local transform for each joint: T * R * S */
    for (u32 j = 0; j < jc; j++) {
        mat4 T, R, S, TR;

        glm_mat4_identity(T);
        glm_translate(T, (f32 *)pose->translations[j]);

        glm_quat_mat4((f32 *)pose->rotations[j], R);

        glm_mat4_identity(S);
        glm_scale(S, (f32 *)pose->scales[j]);

        glm_mat4_mul(T, R, TR);
        glm_mat4_mul(TR, S, local_transforms[j]);
    }

    /* Skeleton root transform */
    mat4 root_xform;
    memcpy(root_xform, skel->root_transform, sizeof(mat4));

    /* Compute global transforms via parent chain */
    for (u32 j = 0; j < jc; j++) {
        i32 parent = skel->parent_indices[j];
        if (parent < 0) {
            glm_mat4_mul(root_xform, local_transforms[j], global_transforms[j]);
        } else {
            glm_mat4_mul(global_transforms[parent], local_transforms[j],
                         global_transforms[j]);
        }
    }

    /* Final skinning matrix = global_transform * inverse_bind_matrix */
    for (u32 j = 0; j < jc; j++) {
        mat4 inv_bind;
        memcpy(inv_bind, skel->inverse_bind_matrices[j], sizeof(mat4));
        glm_mat4_mul(global_transforms[j], inv_bind,
                     (vec4 *)out_joint_matrices[j]);
    }
}

/* --------------------------------------------------------------------------
 * animation_sample — core function: sample clip, build bone chain, produce
 * final joint matrices = global_transform * inverse_bind_matrix
 * ------------------------------------------------------------------------ */

void animation_sample(const SkinnedModel *model, u32 clip_index, f32 time,
                      f32 out_joint_matrices[][16]) {
    const Skeleton *skel = &model->skeleton;

    if (clip_index >= model->clip_count || skel->joint_count == 0) {
        for (u32 j = 0; j < skel->joint_count; j++)
            glm_mat4_identity((vec4 *)out_joint_matrices[j]);
        return;
    }

    const AnimClip *clip = &model->clips[clip_index];

    /* Evaluate into a pose, then convert to matrices.
     * Uses a stack-based arena for backward compat (no arena param). */
    AnimPose pose;
    animation_evaluate_pose(skel, clip, time, &pose);

    u8 scratch_buf[sizeof(mat4) * MAX_JOINTS * 2 + 64];
    Arena scratch;
    arena_init(&scratch, scratch_buf, sizeof(scratch_buf));
    animation_pose_to_matrices(&pose, skel, out_joint_matrices, &scratch);
}

/* --------------------------------------------------------------------------
 * animation_state_init
 * ------------------------------------------------------------------------ */

void animation_state_init(AnimState *state, const SkinnedModel *model) {
    memset(state, 0, sizeof(*state));
    state->speed    = 1.0f;
    state->looping  = true;
    state->current_clip = 0;
    state->joint_count  = model->skeleton.joint_count;

    /* Initialize with rest pose (identity matrices until first update) */
    for (u32 j = 0; j < state->joint_count; j++) {
        glm_mat4_identity((vec4 *)state->joint_matrices[j]);
    }
}

/* --------------------------------------------------------------------------
 * animation_update — advance time and recompute joint matrices
 * ------------------------------------------------------------------------ */

void animation_update(AnimState *state, const SkinnedModel *model, f32 delta_time) {
    if (model->clip_count == 0 || state->current_clip >= model->clip_count) return;

    const AnimClip *clip = &model->clips[state->current_clip];

    state->current_time += delta_time * state->speed;

    if (state->looping && clip->duration > 0.0f) {
        state->current_time = fmodf(state->current_time, clip->duration);
        if (state->current_time < 0.0f)
            state->current_time += clip->duration;
    } else {
        state->current_time = ENGINE_CLAMP(state->current_time, 0.0f, clip->duration);
    }

    state->joint_count = model->skeleton.joint_count;
    animation_sample(model, state->current_clip, state->current_time,
                     state->joint_matrices);
}

/* --------------------------------------------------------------------------
 * animation_blend — per-element matrix lerp between two poses
 * ------------------------------------------------------------------------ */

void animation_blend(const f32 pose_a[][16], const f32 pose_b[][16],
                     u32 joint_count, f32 factor,
                     f32 out_joint_matrices[][16]) {
    f32 inv = 1.0f - factor;
    for (u32 j = 0; j < joint_count; j++) {
        for (u32 i = 0; i < 16; i++) {
            out_joint_matrices[j][i] = pose_a[j][i] * inv + pose_b[j][i] * factor;
        }
    }
}

/* --------------------------------------------------------------------------
 * skinned_model_destroy — free animation clip data
 * ------------------------------------------------------------------------ */

void skinned_model_destroy(SkinnedModel *model) {
    if (!model) return;

    for (u32 c = 0; c < model->clip_count; c++) {
        AnimClip *clip = &model->clips[c];
        for (u32 ch = 0; ch < clip->channel_count; ch++) {
            free(clip->channels[ch].timestamps);
            free(clip->channels[ch].values);
        }
        free(clip->channels);
    }
    free(model->clips);

    model->clips      = NULL;
    model->clip_count = 0;
}
