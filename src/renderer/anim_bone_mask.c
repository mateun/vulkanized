#include "renderer/anim_graph.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Internal: recursively mark a joint and all descendants in the skeleton
 * ------------------------------------------------------------------------ */

static void mark_descendants(const Skeleton *skel, u32 joint_index,
                              f32 weight, BoneMask *mask) {
    mask->weights[joint_index] = weight;
    for (u32 j = 0; j < skel->joint_count; j++) {
        if (skel->parent_indices[j] == (i32)joint_index) {
            mark_descendants(skel, j, weight, mask);
        }
    }
}

/* --------------------------------------------------------------------------
 * bone_mask_create_from_joint — include root_joint_index and all descendants
 * ------------------------------------------------------------------------ */

BoneMask *bone_mask_create_from_joint(const Skeleton *skel,
                                       u32 root_joint_index, f32 weight) {
    if (!skel || root_joint_index >= skel->joint_count) return NULL;

    BoneMask *mask = calloc(1, sizeof(BoneMask));
    if (!mask) return NULL;

    mask->joint_count = skel->joint_count;
    /* All weights start at 0, then mark the subtree */
    mark_descendants(skel, root_joint_index, weight, mask);
    return mask;
}

/* --------------------------------------------------------------------------
 * bone_mask_create_excluding_joint — all joints at weight 1.0 EXCEPT
 * exclude_root and its descendants (which get 0).
 * ------------------------------------------------------------------------ */

BoneMask *bone_mask_create_excluding_joint(const Skeleton *skel,
                                            u32 exclude_root_index) {
    if (!skel || exclude_root_index >= skel->joint_count) return NULL;

    BoneMask *mask = calloc(1, sizeof(BoneMask));
    if (!mask) return NULL;

    mask->joint_count = skel->joint_count;

    /* Start with all weights at 1.0 */
    for (u32 j = 0; j < skel->joint_count; j++)
        mask->weights[j] = 1.0f;

    /* Zero out the excluded subtree */
    mark_descendants(skel, exclude_root_index, 0.0f, mask);
    return mask;
}

/* --------------------------------------------------------------------------
 * bone_mask_destroy
 * ------------------------------------------------------------------------ */

void bone_mask_destroy(BoneMask *mask) {
    free(mask);
}
