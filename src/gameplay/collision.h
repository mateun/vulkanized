#ifndef ENGINE_COLLISION_H
#define ENGINE_COLLISION_H

#include "core/common.h"
#include "renderer/renderer_types.h"

/* ---- Collision pair (returned by batch checks) ---- */

typedef struct {
    i32 index_a;  /* index into array A */
    i32 index_b;  /* index into array B */
} CollisionPair;

/* ---- Circle vs circle ---- */

/* Returns true if two circles overlap.
 * All positions are center (x, y), radii are world-space. */
bool collision_circle_circle(f32 ax, f32 ay, f32 ar,
                             f32 bx, f32 by, f32 br);

/* ---- Single circle vs instance array ---- */

/* Check one circle against an array of InstanceData.
 * instance_radius is a uniform collision radius for every instance.
 * Returns the index of the FIRST hit, or -1 if none. */
i32 collision_circle_vs_instances(f32 cx, f32 cy, f32 radius,
                                  const InstanceData *instances, i32 count,
                                  f32 instance_radius);

/* ---- Array vs array (e.g. bullets vs enemies) ---- */

/* Brute-force check every (a[i], b[j]) pair.
 * Writes hit pairs into out_pairs (up to max_pairs).
 * Returns the number of pairs written.
 * Pairs are ordered: index_a = index in array A, index_b = index in array B. */
i32 collision_instances_vs_instances(const InstanceData *a, i32 a_count, f32 a_radius,
                                     const InstanceData *b, i32 b_count, f32 b_radius,
                                     CollisionPair *out_pairs, i32 max_pairs);

#endif /* ENGINE_COLLISION_H */
