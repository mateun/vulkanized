#ifndef ENGINE_ANIM_BLEND_H
#define ENGINE_ANIM_BLEND_H

#include "core/common.h"
#include "core/arena.h"
#include "renderer/animation_types.h"

/* Forward declarations */
typedef struct BoneMask BoneMask;

/* Blend two poses: out = lerp(a, b, factor).
 * Translations/scales: vec3 lerp.
 * Rotations: quaternion slerp (shortest-path). */
void pose_blend(const AnimPose *a, const AnimPose *b, u32 joint_count,
                f32 factor, AnimPose *out);

/* Masked blend: out[j] = lerp(base[j], overlay[j], mask->weights[j] * factor).
 * Joints with mask weight 0 are left as base. */
void pose_blend_masked(const AnimPose *base, const AnimPose *overlay,
                        u32 joint_count, const BoneMask *mask,
                        f32 factor, AnimPose *out);

/* Additive blend: out = base + (additive - reference) * weight.
 * For rotations: out = base * slerp(identity, inv(ref) * additive, weight).
 * mask: optional (NULL = all joints). */
void pose_blend_additive(const AnimPose *base, const AnimPose *additive,
                          const AnimPose *reference, u32 joint_count,
                          const BoneMask *mask, f32 weight, AnimPose *out);

/* Copy rest pose from skeleton into an AnimPose buffer. */
void pose_from_rest(const Skeleton *skel, AnimPose *out);

/* Copy one pose to another. */
void pose_copy(const AnimPose *src, u32 joint_count, AnimPose *dst);

#endif /* ENGINE_ANIM_BLEND_H */
