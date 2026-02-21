#ifndef ENGINE_ANIMATION_TYPES_H
#define ENGINE_ANIMATION_TYPES_H

#include "core/common.h"

#define MAX_JOINTS 128

/* ---- Interpolation type (matches glTF) ---- */

typedef enum {
    ANIM_INTERP_STEP,
    ANIM_INTERP_LINEAR,
    ANIM_INTERP_CUBICSPLINE,
} AnimInterpolation;

/* ---- Animated property type ---- */

typedef enum {
    ANIM_PATH_TRANSLATION,
    ANIM_PATH_ROTATION,
    ANIM_PATH_SCALE,
} AnimPathType;

/* ---- Single animation channel: one joint's T, R, or S over time ---- */

typedef struct {
    u32               target_joint;   /* index into skeleton joint array */
    AnimPathType      path;           /* which property to animate */
    AnimInterpolation interpolation;

    f32              *timestamps;     /* [keyframe_count] monotonically increasing */
    f32              *values;         /* translation: [count*3], rotation: [count*4], scale: [count*3] */
                                      /* cubicspline: 3x (in-tangent, value, out-tangent per keyframe) */
    u32               keyframe_count;
} AnimChannel;

/* ---- Animation clip (one cgltf_animation) ---- */

typedef struct {
    char             name[64];       /* animation name from glTF */
    f32              duration;       /* max timestamp across all channels */
    AnimChannel     *channels;       /* array of channels */
    u32              channel_count;
} AnimClip;

/* ---- Skeleton: bone hierarchy from a glTF skin ---- */

typedef struct {
    u32   joint_count;
    i32   parent_indices[MAX_JOINTS];               /* -1 = root joint */
    f32   inverse_bind_matrices[MAX_JOINTS][16];    /* mat4 per joint, column-major */
    f32   rest_translations[MAX_JOINTS][3];
    f32   rest_rotations[MAX_JOINTS][4];            /* quaternion [x, y, z, w] */
    f32   rest_scales[MAX_JOINTS][3];
    f32   root_transform[16];                       /* world transform of skeleton root node */
} Skeleton;

/* ---- Local-space pose: intermediate format for blending ---- */

typedef struct {
    f32   translations[MAX_JOINTS][3];
    f32   rotations[MAX_JOINTS][4];      /* quaternion [x, y, z, w] */
    f32   scales[MAX_JOINTS][3];
} AnimPose;

/* ---- Runtime animation state (owned by game, one per animated instance) ---- */

typedef struct {
    f32   current_time;              /* current playback time in seconds */
    f32   speed;                     /* playback speed multiplier (1.0 = normal) */
    bool  looping;                   /* wrap time at clip duration? */
    u32   current_clip;              /* index into the model's clip array */

    /* Output: computed joint matrices ready for GPU upload */
    f32   joint_matrices[MAX_JOINTS][16];  /* mat4 per joint, column-major */
    u32   joint_count;                      /* active joint count from skeleton */
} AnimState;

/* ---- Skinned model: geometry + skeleton + animations ---- */

typedef struct {
    u32        mesh_handle;          /* MeshHandle for the skinned geometry */
    Skeleton   skeleton;
    AnimClip  *clips;                /* array of animation clips */
    u32        clip_count;
} SkinnedModel;

#endif /* ENGINE_ANIMATION_TYPES_H */
