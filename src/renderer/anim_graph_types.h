#ifndef ENGINE_ANIM_GRAPH_TYPES_H
#define ENGINE_ANIM_GRAPH_TYPES_H

#include "core/common.h"
#include "renderer/animation_types.h"

/* ================================================================
 * BONE MASK — per-joint weight for layered blending
 * ================================================================ */

typedef struct BoneMask {
    f32 weights[MAX_JOINTS];   /* 0.0 = excluded, 1.0 = fully included */
    u32 joint_count;
} BoneMask;

/* ================================================================
 * ANIMATION EVENTS — per-clip point events
 * ================================================================ */

typedef void (*AnimEventCallback)(void *user_data, u32 event_id, const char *name);

typedef struct {
    f32              time;       /* seconds into clip */
    u32              event_id;   /* user-defined ID */
    char             name[32];   /* event name (e.g. "footstep_left") */
} AnimEvent;

typedef struct {
    AnimEvent       *events;     /* sorted by time, owned by the graph def */
    u32              event_count;
} AnimEventList;

/* ================================================================
 * GRAPH PARAMETERS — named floats and bools
 * ================================================================ */

#define ANIM_MAX_PARAMS         16
#define ANIM_PARAM_NAME_LEN     32

typedef enum {
    ANIM_PARAM_FLOAT,
    ANIM_PARAM_BOOL,
} AnimParamType;

typedef struct {
    char           name[ANIM_PARAM_NAME_LEN];
    AnimParamType  type;
    union {
        f32  f;
        bool b;
    } default_value;
} AnimParamDef;

/* Runtime parameter storage (per instance) */
typedef struct {
    union {
        f32  f;
        bool b;
    } values[ANIM_MAX_PARAMS];
} AnimParamValues;

/* ================================================================
 * 1D BLEND SPACE
 * ================================================================ */

#define ANIM_BLEND1D_MAX_CLIPS  8

typedef struct {
    f32 position;     /* position on the 1D axis */
    u32 clip_index;   /* index into SkinnedModel.clips[] */
} BlendSpace1DEntry;

typedef struct {
    BlendSpace1DEntry entries[ANIM_BLEND1D_MAX_CLIPS];
    u32               entry_count;
    u32               param_index;   /* which parameter drives this blend space */
} BlendSpace1D;

/* ================================================================
 * 2D BLEND SPACE
 * ================================================================ */

#define ANIM_BLEND2D_MAX_CLIPS  16

typedef struct {
    f32 position[2];   /* (x, y) position on the 2D plane */
    u32 clip_index;    /* index into SkinnedModel.clips[] */
} BlendSpace2DEntry;

typedef struct {
    BlendSpace2DEntry  entries[ANIM_BLEND2D_MAX_CLIPS];
    u32                entry_count;
    u32                param_x_index;   /* parameter for X axis */
    u32                param_y_index;   /* parameter for Y axis */
} BlendSpace2D;

/* ================================================================
 * STATE NODE — what a state contains
 * ================================================================ */

typedef enum {
    ANIM_STATE_CLIP,         /* single animation clip */
    ANIM_STATE_BLEND1D,      /* 1D blend space */
    ANIM_STATE_BLEND2D,      /* 2D blend space */
} AnimStateType;

typedef struct {
    char             name[ANIM_PARAM_NAME_LEN];
    AnimStateType    type;
    union {
        u32          clip_index;     /* ANIM_STATE_CLIP */
        BlendSpace1D blend1d;        /* ANIM_STATE_BLEND1D */
        BlendSpace2D blend2d;        /* ANIM_STATE_BLEND2D */
    } data;
    f32              speed;          /* playback speed multiplier (default 1.0) */
    bool             looping;        /* wrap time at clip duration? */
    AnimEventList   *events;         /* NULL = no events for this state */
} AnimStateNode;

/* ================================================================
 * TRANSITION CONDITIONS
 * ================================================================ */

typedef enum {
    ANIM_COND_FLOAT_GT,       /* param > threshold */
    ANIM_COND_FLOAT_LT,       /* param < threshold */
    ANIM_COND_FLOAT_GE,       /* param >= threshold */
    ANIM_COND_FLOAT_LE,       /* param <= threshold */
    ANIM_COND_BOOL_TRUE,      /* param == true */
    ANIM_COND_BOOL_FALSE,     /* param == false */
    ANIM_COND_CALLBACK,       /* user function pointer returns true */
} AnimConditionType;

typedef bool (*AnimConditionCallback)(const void *user_data, const AnimParamValues *params);

#define ANIM_MAX_CONDITIONS_PER_TRANSITION 4

typedef struct {
    AnimConditionType type;
    u32               param_index;    /* which parameter to check */
    f32               threshold;      /* for float comparisons */
    AnimConditionCallback callback;   /* for ANIM_COND_CALLBACK */
    void             *callback_data;  /* for ANIM_COND_CALLBACK */
} AnimCondition;

/* ================================================================
 * TRANSITION — edge between states
 * ================================================================ */

typedef struct {
    u32             source_state;    /* index into layer's states[] */
    u32             target_state;    /* index into layer's states[] */
    f32             duration;        /* crossfade duration in seconds (0 = instant) */
    AnimCondition   conditions[ANIM_MAX_CONDITIONS_PER_TRANSITION];
    u32             condition_count; /* all conditions must be true (AND logic) */
    bool            has_exit_time;   /* if true, only transitions after exit_time */
    f32             exit_time;       /* normalized time (0-1) within clip */
} AnimTransition;

/* ================================================================
 * LAYER — one state machine with an optional bone mask
 * ================================================================ */

typedef enum {
    ANIM_LAYER_OVERRIDE,    /* replace base pose (weighted) */
    ANIM_LAYER_ADDITIVE,    /* add delta on top of base */
} AnimLayerBlendMode;

#define ANIM_MAX_STATES_PER_LAYER       16
#define ANIM_MAX_TRANSITIONS_PER_LAYER  32

typedef struct {
    char                name[ANIM_PARAM_NAME_LEN];
    AnimStateNode       states[ANIM_MAX_STATES_PER_LAYER];
    u32                 state_count;
    AnimTransition      transitions[ANIM_MAX_TRANSITIONS_PER_LAYER];
    u32                 transition_count;
    u32                 default_state;     /* index of initial state */
    BoneMask           *bone_mask;         /* NULL = all bones at weight 1.0 */
    f32                 weight;            /* layer weight (0-1), default 1.0 */
    AnimLayerBlendMode  blend_mode;        /* OVERRIDE or ADDITIVE */
} AnimLayerDef;

/* ================================================================
 * GRAPH DEFINITION — the full animation graph (shared/static)
 * ================================================================ */

#define ANIM_MAX_LAYERS 4

typedef struct {
    AnimParamDef    params[ANIM_MAX_PARAMS];
    u32             param_count;

    AnimLayerDef    layers[ANIM_MAX_LAYERS];
    u32             layer_count;
} AnimGraphDef;

/* ================================================================
 * LAYER RUNTIME STATE (per-entity per-layer)
 * ================================================================ */

typedef struct {
    u32   current_state;        /* index into layer def's states[] */
    f32   state_time;           /* seconds elapsed in current state */
    f32   state_normalized;     /* 0-1 normalized time for current state's clip(s) */

    /* Transition blending */
    bool  transitioning;
    u32   prev_state;           /* state we are transitioning FROM */
    f32   prev_state_time;      /* frozen or continuing time of prev state */
    f32   transition_elapsed;   /* seconds into the transition */
    f32   transition_duration;  /* total transition time */

    /* Event tracking */
    f32   prev_event_time;      /* last time at which events were checked */
} AnimLayerState;

/* ================================================================
 * GRAPH INSTANCE — per-entity runtime state
 * ================================================================ */

typedef struct {
    const AnimGraphDef   *def;    /* shared definition (not owned) */
    AnimParamValues       params; /* runtime parameter values */
    AnimLayerState        layer_states[ANIM_MAX_LAYERS];

    /* Event callback */
    AnimEventCallback     event_callback;
    void                 *event_user_data;

    /* Output */
    f32   joint_matrices[MAX_JOINTS][16];
    u32   joint_count;
} AnimGraphInstance;

#endif /* ENGINE_ANIM_GRAPH_TYPES_H */
