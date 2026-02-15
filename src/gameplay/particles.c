#include "gameplay/particles.h"

#include <math.h>    /* sinf, cosf */
#include <stdlib.h>  /* rand, RAND_MAX */

/* --------------------------------------------------------------------------
 * Helper: random float in [min, max]
 * ------------------------------------------------------------------------ */

static f32 rand_range(f32 min, f32 max) {
    return min + ((f32)rand() / (f32)RAND_MAX) * (max - min);
}

/* --------------------------------------------------------------------------
 * Emit particles in a 360-degree circular burst
 * ------------------------------------------------------------------------ */

i32 particles_emit(const ParticleEmitter *emitter,
                   Particle *particles, i32 current_count, i32 max_capacity)
{
    i32 space = max_capacity - current_count;
    i32 num   = emitter->count;
    if (num > space) num = space;
    if (num <= 0) return 0;

    const f32 TWO_PI    = 6.28318530f;
    const f32 angle_step = TWO_PI / (f32)num;

    for (i32 i = 0; i < num; i++) {
        Particle *p = &particles[current_count + i];

        /* Direction: evenly spaced around a circle with slight jitter */
        f32 angle = (f32)i * angle_step + rand_range(-0.15f, 0.15f);
        f32 speed = rand_range(emitter->speed_min, emitter->speed_max);

        p->position[0] = emitter->position[0];
        p->position[1] = emitter->position[1];
        p->velocity[0] = cosf(angle) * speed;
        p->velocity[1] = sinf(angle) * speed;

        p->color[0] = emitter->color[0];
        p->color[1] = emitter->color[1];
        p->color[2] = emitter->color[2];

        p->lifetime     = rand_range(emitter->lifetime_min, emitter->lifetime_max);
        p->max_lifetime = p->lifetime;

        p->rotation         = rand_range(0.0f, TWO_PI);
        p->angular_velocity = rand_range(emitter->angular_velocity_min,
                                         emitter->angular_velocity_max);

        p->scale = emitter->scale;
    }

    return num;
}

/* --------------------------------------------------------------------------
 * Update: move, rotate, tick lifetime, swap-remove dead
 * ------------------------------------------------------------------------ */

i32 particles_update(Particle *particles, i32 count, f32 delta_time)
{
    for (i32 i = 0; i < count; ) {
        Particle *p = &particles[i];

        p->lifetime -= delta_time;

        if (p->lifetime <= 0.0f) {
            /* Swap-remove: replace with last element */
            particles[i] = particles[--count];
            continue;
        }

        /* Move */
        p->position[0] += p->velocity[0] * delta_time;
        p->position[1] += p->velocity[1] * delta_time;

        /* Rotate */
        p->rotation += p->angular_velocity * delta_time;

        i++;
    }

    return count;
}

/* --------------------------------------------------------------------------
 * Convert to InstanceData (applies fade + shrink from lifetime ratio)
 * ------------------------------------------------------------------------ */

i32 particles_to_instances(const Particle *particles, i32 count,
                           InstanceData *out_instances, i32 max_instances)
{
    i32 num = count;
    if (num > max_instances) num = max_instances;

    for (i32 i = 0; i < num; i++) {
        const Particle *p    = &particles[i];
        InstanceData   *inst = &out_instances[i];

        /* Lifetime ratio: 1.0 = just born, 0.0 = about to die */
        f32 t = p->lifetime / p->max_lifetime;

        inst->position[0] = p->position[0];
        inst->position[1] = p->position[1];
        inst->rotation     = p->rotation;

        /* Quadratic scale fade — shrinks quickly near death */
        f32 s = p->scale * t * t;
        inst->scale[0] = s;
        inst->scale[1] = s;

        /* Linear color fade */
        inst->color[0] = p->color[0] * t;
        inst->color[1] = p->color[1] * t;
        inst->color[2] = p->color[2] * t;

        /* No sprite sheet for particles — use full texture (0,0 signals passthrough) */
        inst->uv_offset[0] = 0.0f;
        inst->uv_offset[1] = 0.0f;
        inst->uv_scale[0]  = 0.0f;
        inst->uv_scale[1]  = 0.0f;
    }

    return num;
}
