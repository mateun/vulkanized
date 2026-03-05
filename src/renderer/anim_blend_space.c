#include "renderer/anim_graph_types.h"
#include "renderer/anim_blend.h"
#include "renderer/animation.h"
#include "core/log.h"

#include <math.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * blend_space_1d_evaluate — sample a 1D blend space at a given parameter value
 *
 * Finds the two bracketing entries, samples both clips at time-synchronized
 * positions, and blends the resulting poses.
 * ------------------------------------------------------------------------ */

void blend_space_1d_evaluate(const BlendSpace1D *space,
                              f32 param_value,
                              const SkinnedModel *model,
                              f32 normalized_time,
                              Arena *scratch,
                              AnimPose *out_pose) {
    if (space->entry_count == 0) {
        pose_from_rest(&model->skeleton, out_pose);
        return;
    }

    /* Clamp param to range */
    f32 lo_pos = space->entries[0].position;
    f32 hi_pos = space->entries[space->entry_count - 1].position;
    f32 p = param_value;
    if (p <= lo_pos) p = lo_pos;
    if (p >= hi_pos) p = hi_pos;

    /* Single entry: just sample it */
    if (space->entry_count == 1) {
        u32 ci = space->entries[0].clip_index;
        if (ci < model->clip_count) {
            f32 t = normalized_time * model->clips[ci].duration;
            animation_evaluate_pose(&model->skeleton, &model->clips[ci], t, out_pose);
        } else {
            pose_from_rest(&model->skeleton, out_pose);
        }
        return;
    }

    /* Find bracketing entries */
    u32 lo = 0, hi = 1;
    for (u32 i = 1; i < space->entry_count; i++) {
        if (space->entries[i].position >= p) {
            hi = i;
            lo = i - 1;
            break;
        }
    }

    /* Compute blend factor */
    f32 range = space->entries[hi].position - space->entries[lo].position;
    f32 factor = (range > 1e-6f) ? (p - space->entries[lo].position) / range : 0.0f;

    /* Sample both clips at time-synchronized positions */
    u32 ci_a = space->entries[lo].clip_index;
    u32 ci_b = space->entries[hi].clip_index;

    AnimPose *pose_a = arena_push(scratch, AnimPose);
    AnimPose *pose_b = arena_push(scratch, AnimPose);

    if (!pose_a || !pose_b) {
        pose_from_rest(&model->skeleton, out_pose);
        return;
    }

    if (ci_a < model->clip_count) {
        f32 t = normalized_time * model->clips[ci_a].duration;
        animation_evaluate_pose(&model->skeleton, &model->clips[ci_a], t, pose_a);
    } else {
        pose_from_rest(&model->skeleton, pose_a);
    }

    if (ci_b < model->clip_count) {
        f32 t = normalized_time * model->clips[ci_b].duration;
        animation_evaluate_pose(&model->skeleton, &model->clips[ci_b], t, pose_b);
    } else {
        pose_from_rest(&model->skeleton, pose_b);
    }

    /* Blend */
    pose_blend(pose_a, pose_b, model->skeleton.joint_count, factor, out_pose);
}

/* --------------------------------------------------------------------------
 * blend_space_2d_evaluate — sample a 2D blend space at (px, py)
 *
 * Uses nearest-3 approach: find the 3 closest entries and compute
 * barycentric weights for interpolation.
 * ------------------------------------------------------------------------ */

void blend_space_2d_evaluate(const BlendSpace2D *space,
                              f32 param_x, f32 param_y,
                              const SkinnedModel *model,
                              f32 normalized_time,
                              Arena *scratch,
                              AnimPose *out_pose) {
    if (space->entry_count == 0) {
        pose_from_rest(&model->skeleton, out_pose);
        return;
    }

    /* Single entry */
    if (space->entry_count == 1) {
        u32 ci = space->entries[0].clip_index;
        if (ci < model->clip_count) {
            f32 t = normalized_time * model->clips[ci].duration;
            animation_evaluate_pose(&model->skeleton, &model->clips[ci], t, out_pose);
        } else {
            pose_from_rest(&model->skeleton, out_pose);
        }
        return;
    }

    /* Two entries: linear blend */
    if (space->entry_count == 2) {
        f32 dx = space->entries[1].position[0] - space->entries[0].position[0];
        f32 dy = space->entries[1].position[1] - space->entries[0].position[1];
        f32 px = param_x - space->entries[0].position[0];
        f32 py = param_y - space->entries[0].position[1];
        f32 len2 = dx*dx + dy*dy;
        f32 t_proj = (len2 > 1e-6f) ? (px*dx + py*dy) / len2 : 0.0f;
        if (t_proj < 0.0f) t_proj = 0.0f;
        if (t_proj > 1.0f) t_proj = 1.0f;

        AnimPose *pa = arena_push(scratch, AnimPose);
        AnimPose *pb = arena_push(scratch, AnimPose);
        if (!pa || !pb) { pose_from_rest(&model->skeleton, out_pose); return; }

        u32 ci_a = space->entries[0].clip_index;
        u32 ci_b = space->entries[1].clip_index;
        if (ci_a < model->clip_count) {
            f32 ct = normalized_time * model->clips[ci_a].duration;
            animation_evaluate_pose(&model->skeleton, &model->clips[ci_a], ct, pa);
        } else { pose_from_rest(&model->skeleton, pa); }
        if (ci_b < model->clip_count) {
            f32 ct = normalized_time * model->clips[ci_b].duration;
            animation_evaluate_pose(&model->skeleton, &model->clips[ci_b], ct, pb);
        } else { pose_from_rest(&model->skeleton, pb); }

        pose_blend(pa, pb, model->skeleton.joint_count, t_proj, out_pose);
        return;
    }

    /* Find 3 nearest entries by distance */
    f32 dists[ANIM_BLEND2D_MAX_CLIPS];
    for (u32 i = 0; i < space->entry_count; i++) {
        f32 dx = param_x - space->entries[i].position[0];
        f32 dy = param_y - space->entries[i].position[1];
        dists[i] = dx*dx + dy*dy;
    }

    /* Find indices of 3 nearest */
    u32 idx[3] = {0, 1, 2};
    /* Sort first 3 */
    for (u32 i = 0; i < 3 && i < space->entry_count; i++) {
        for (u32 k = i + 1; k < space->entry_count; k++) {
            if (dists[k] < dists[idx[i]]) {
                u32 tmp = idx[i]; idx[i] = k;
                /* Push old idx[i] back into remaining candidates */
                if (i + 1 < 3) {
                    /* Check if the displaced index should replace any of the later slots */
                    for (u32 m = i + 1; m < 3; m++) {
                        if (dists[tmp] < dists[idx[m]]) {
                            u32 tmp2 = idx[m]; idx[m] = tmp; tmp = tmp2;
                        }
                    }
                }
            }
        }
    }

    /* Compute barycentric coordinates for the triangle formed by the 3 nearest */
    f32 x0 = space->entries[idx[0]].position[0], y0 = space->entries[idx[0]].position[1];
    f32 x1 = space->entries[idx[1]].position[0], y1 = space->entries[idx[1]].position[1];
    f32 x2 = space->entries[idx[2]].position[0], y2 = space->entries[idx[2]].position[1];

    f32 det = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2);
    f32 w0, w1, w2;
    if (fabsf(det) < 1e-6f) {
        /* Degenerate triangle: use inverse-distance weighting */
        f32 d0 = sqrtf(dists[idx[0]]) + 1e-6f;
        f32 d1 = sqrtf(dists[idx[1]]) + 1e-6f;
        f32 d2 = sqrtf(dists[idx[2]]) + 1e-6f;
        f32 inv_sum = 1.0f / (1.0f/d0 + 1.0f/d1 + 1.0f/d2);
        w0 = (1.0f/d0) * inv_sum;
        w1 = (1.0f/d1) * inv_sum;
        w2 = (1.0f/d2) * inv_sum;
    } else {
        w0 = ((y1 - y2) * (param_x - x2) + (x2 - x1) * (param_y - y2)) / det;
        w1 = ((y2 - y0) * (param_x - x2) + (x0 - x2) * (param_y - y2)) / det;
        w2 = 1.0f - w0 - w1;

        /* Clamp and renormalize */
        if (w0 < 0.0f) w0 = 0.0f;
        if (w1 < 0.0f) w1 = 0.0f;
        if (w2 < 0.0f) w2 = 0.0f;
        f32 ws = w0 + w1 + w2;
        if (ws > 1e-6f) { w0 /= ws; w1 /= ws; w2 /= ws; }
        else { w0 = 1.0f; w1 = 0.0f; w2 = 0.0f; }
    }

    /* Sample the 3 clips */
    AnimPose *p0 = arena_push(scratch, AnimPose);
    AnimPose *p1 = arena_push(scratch, AnimPose);
    AnimPose *p2 = arena_push(scratch, AnimPose);
    AnimPose *tmp = arena_push(scratch, AnimPose);
    if (!p0 || !p1 || !p2 || !tmp) {
        pose_from_rest(&model->skeleton, out_pose);
        return;
    }

    for (u32 i = 0; i < 3; i++) {
        AnimPose *dst = (i == 0) ? p0 : (i == 1) ? p1 : p2;
        u32 ci = space->entries[idx[i]].clip_index;
        if (ci < model->clip_count) {
            f32 t = normalized_time * model->clips[ci].duration;
            animation_evaluate_pose(&model->skeleton, &model->clips[ci], t, dst);
        } else {
            pose_from_rest(&model->skeleton, dst);
        }
    }

    /* 3-way blend: blend(p0, p1, w1/(w0+w1)) -> tmp, then blend(tmp, p2, w2) */
    u32 jc = model->skeleton.joint_count;
    if (w0 + w1 > 1e-6f) {
        f32 f01 = w1 / (w0 + w1);
        pose_blend(p0, p1, jc, f01, tmp);
    } else {
        pose_copy(p2, jc, tmp);
        w2 = 1.0f;
    }
    pose_blend(tmp, p2, jc, w2, out_pose);
}
