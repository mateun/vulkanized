#include "renderer/anim_blend.h"
#include "renderer/anim_graph_types.h"

#include <cglm/quat.h>
#include <cglm/vec3.h>

#include <string.h>

/* --------------------------------------------------------------------------
 * pose_blend — blend two poses by factor (0.0 = a, 1.0 = b)
 * Translations/scales: vec3 lerp.  Rotations: quaternion slerp.
 * ------------------------------------------------------------------------ */

void pose_blend(const AnimPose *a, const AnimPose *b, u32 joint_count,
                f32 factor, AnimPose *out) {
    f32 inv = 1.0f - factor;
    for (u32 j = 0; j < joint_count; j++) {
        /* Translation: lerp */
        for (u32 i = 0; i < 3; i++)
            out->translations[j][i] = a->translations[j][i] * inv + b->translations[j][i] * factor;

        /* Rotation: slerp with shortest-path correction */
        f32 qa[4], qb[4];
        memcpy(qa, a->rotations[j], sizeof(f32) * 4);
        memcpy(qb, b->rotations[j], sizeof(f32) * 4);

        /* Ensure shortest path */
        f32 dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
        if (dot < 0.0f) {
            qb[0] = -qb[0]; qb[1] = -qb[1]; qb[2] = -qb[2]; qb[3] = -qb[3];
        }
        glm_quat_slerp(qa, qb, factor, out->rotations[j]);

        /* Scale: lerp */
        for (u32 i = 0; i < 3; i++)
            out->scales[j][i] = a->scales[j][i] * inv + b->scales[j][i] * factor;
    }
}

/* --------------------------------------------------------------------------
 * pose_blend_masked — blend with per-joint weights from bone mask
 * ------------------------------------------------------------------------ */

void pose_blend_masked(const AnimPose *base, const AnimPose *overlay,
                        u32 joint_count, const BoneMask *mask,
                        f32 factor, AnimPose *out) {
    for (u32 j = 0; j < joint_count; j++) {
        f32 w = mask->weights[j] * factor;
        if (w < 1e-6f) {
            /* No influence — copy base */
            memcpy(out->translations[j], base->translations[j], sizeof(f32) * 3);
            memcpy(out->rotations[j],    base->rotations[j],    sizeof(f32) * 4);
            memcpy(out->scales[j],       base->scales[j],       sizeof(f32) * 3);
            continue;
        }

        f32 inv = 1.0f - w;

        /* Translation: lerp */
        for (u32 i = 0; i < 3; i++)
            out->translations[j][i] = base->translations[j][i] * inv + overlay->translations[j][i] * w;

        /* Rotation: slerp with shortest-path */
        f32 qa[4], qb[4];
        memcpy(qa, base->rotations[j], sizeof(f32) * 4);
        memcpy(qb, overlay->rotations[j], sizeof(f32) * 4);

        f32 dot = qa[0]*qb[0] + qa[1]*qb[1] + qa[2]*qb[2] + qa[3]*qb[3];
        if (dot < 0.0f) {
            qb[0] = -qb[0]; qb[1] = -qb[1]; qb[2] = -qb[2]; qb[3] = -qb[3];
        }
        glm_quat_slerp(qa, qb, w, out->rotations[j]);

        /* Scale: lerp */
        for (u32 i = 0; i < 3; i++)
            out->scales[j][i] = base->scales[j][i] * inv + overlay->scales[j][i] * w;
    }
}

/* --------------------------------------------------------------------------
 * pose_blend_additive — add pose delta on top of base
 *   out_t = base_t + (additive_t - reference_t) * weight
 *   out_r = base_r * slerp(identity, inv(ref_r) * additive_r, weight)
 *   out_s = base_s + (additive_s - reference_s) * weight
 * ------------------------------------------------------------------------ */

void pose_blend_additive(const AnimPose *base, const AnimPose *additive,
                          const AnimPose *reference, u32 joint_count,
                          const BoneMask *mask, f32 weight, AnimPose *out) {
    f32 identity_q[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    for (u32 j = 0; j < joint_count; j++) {
        f32 w = weight;
        if (mask) w *= mask->weights[j];
        if (w < 1e-6f) {
            memcpy(out->translations[j], base->translations[j], sizeof(f32) * 3);
            memcpy(out->rotations[j],    base->rotations[j],    sizeof(f32) * 4);
            memcpy(out->scales[j],       base->scales[j],       sizeof(f32) * 3);
            continue;
        }

        /* Translation: additive delta */
        for (u32 i = 0; i < 3; i++)
            out->translations[j][i] = base->translations[j][i]
                + (additive->translations[j][i] - reference->translations[j][i]) * w;

        /* Rotation: q_delta = inv(ref) * additive, then base * slerp(id, delta, w) */
        f32 ref_inv[4];
        glm_quat_inv((f32 *)reference->rotations[j], ref_inv);

        f32 q_delta[4];
        glm_quat_mul(ref_inv, (f32 *)additive->rotations[j], q_delta);

        /* Ensure shortest path for delta */
        f32 dot = q_delta[0]*identity_q[0] + q_delta[1]*identity_q[1]
                + q_delta[2]*identity_q[2] + q_delta[3]*identity_q[3];
        if (dot < 0.0f) {
            q_delta[0] = -q_delta[0]; q_delta[1] = -q_delta[1];
            q_delta[2] = -q_delta[2]; q_delta[3] = -q_delta[3];
        }

        f32 q_delta_weighted[4];
        glm_quat_slerp(identity_q, q_delta, w, q_delta_weighted);

        glm_quat_mul((f32 *)base->rotations[j], q_delta_weighted, out->rotations[j]);
        glm_quat_normalize(out->rotations[j]);

        /* Scale: additive delta */
        for (u32 i = 0; i < 3; i++)
            out->scales[j][i] = base->scales[j][i]
                + (additive->scales[j][i] - reference->scales[j][i]) * w;
    }
}

/* --------------------------------------------------------------------------
 * pose_from_rest — copy rest pose from skeleton
 * ------------------------------------------------------------------------ */

void pose_from_rest(const Skeleton *skel, AnimPose *out) {
    for (u32 j = 0; j < skel->joint_count; j++) {
        memcpy(out->translations[j], skel->rest_translations[j], sizeof(f32) * 3);
        memcpy(out->rotations[j],    skel->rest_rotations[j],    sizeof(f32) * 4);
        memcpy(out->scales[j],       skel->rest_scales[j],       sizeof(f32) * 3);
    }
}

/* --------------------------------------------------------------------------
 * pose_copy — copy pose data
 * ------------------------------------------------------------------------ */

void pose_copy(const AnimPose *src, u32 joint_count, AnimPose *dst) {
    for (u32 j = 0; j < joint_count; j++) {
        memcpy(dst->translations[j], src->translations[j], sizeof(f32) * 3);
        memcpy(dst->rotations[j],    src->rotations[j],    sizeof(f32) * 4);
        memcpy(dst->scales[j],       src->scales[j],       sizeof(f32) * 3);
    }
}
