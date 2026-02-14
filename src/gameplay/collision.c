#include "gameplay/collision.h"

/* --------------------------------------------------------------------------
 * Circle vs circle (squared-distance, no sqrt)
 * ------------------------------------------------------------------------ */

bool collision_circle_circle(f32 ax, f32 ay, f32 ar,
                             f32 bx, f32 by, f32 br)
{
    f32 dx = bx - ax;
    f32 dy = by - ay;
    f32 dist_sq = dx * dx + dy * dy;
    f32 radii   = ar + br;
    return dist_sq <= radii * radii;
}

/* --------------------------------------------------------------------------
 * Single circle vs instance array — returns first hit index or -1
 * ------------------------------------------------------------------------ */

i32 collision_circle_vs_instances(f32 cx, f32 cy, f32 radius,
                                  const InstanceData *instances, i32 count,
                                  f32 instance_radius)
{
    f32 radii    = radius + instance_radius;
    f32 radii_sq = radii * radii;

    for (i32 i = 0; i < count; i++) {
        f32 dx = instances[i].position[0] - cx;
        f32 dy = instances[i].position[1] - cy;
        if (dx * dx + dy * dy <= radii_sq) {
            return i;
        }
    }
    return -1;
}

/* --------------------------------------------------------------------------
 * Array vs array — writes collision pairs, returns count
 * ------------------------------------------------------------------------ */

i32 collision_instances_vs_instances(const InstanceData *a, i32 a_count, f32 a_radius,
                                     const InstanceData *b, i32 b_count, f32 b_radius,
                                     CollisionPair *out_pairs, i32 max_pairs)
{
    f32 radii    = a_radius + b_radius;
    f32 radii_sq = radii * radii;
    i32 num_hits = 0;

    for (i32 i = 0; i < a_count && num_hits < max_pairs; i++) {
        f32 ax = a[i].position[0];
        f32 ay = a[i].position[1];

        for (i32 j = 0; j < b_count && num_hits < max_pairs; j++) {
            f32 dx = b[j].position[0] - ax;
            f32 dy = b[j].position[1] - ay;
            if (dx * dx + dy * dy <= radii_sq) {
                out_pairs[num_hits].index_a = i;
                out_pairs[num_hits].index_b = j;
                num_hits++;
            }
        }
    }
    return num_hits;
}
