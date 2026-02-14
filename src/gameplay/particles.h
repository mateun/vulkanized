#ifndef ENGINE_PARTICLES_H
#define ENGINE_PARTICLES_H

#include "core/common.h"
#include "renderer/renderer_types.h"

/* ---- Particle (per-particle simulation state) ---- */

typedef struct {
    f32 position[2];       /* world x, y */
    f32 velocity[2];       /* world units/sec */
    f32 color[3];          /* initial HDR color (fade computed from lifetime ratio) */
    f32 lifetime;          /* seconds remaining (counts down to 0) */
    f32 max_lifetime;      /* original lifetime (for fade ratio: t = lifetime/max_lifetime) */
    f32 rotation;          /* radians */
    f32 angular_velocity;  /* radians/sec */
    f32 scale;             /* uniform initial scale (shrinks with lifetime) */
} Particle;

/* ---- Emitter config (describes a burst of particles) ---- */

typedef struct {
    f32 position[2];              /* spawn center (x, y) */
    f32 color[3];                 /* HDR color for all particles */
    i32 count;                    /* number of particles to emit */
    f32 speed_min;                /* minimum outward speed (world units/sec) */
    f32 speed_max;                /* maximum outward speed */
    f32 lifetime_min;             /* minimum lifetime (seconds) */
    f32 lifetime_max;             /* maximum lifetime */
    f32 scale;                    /* particle size (uniform) */
    f32 angular_velocity_min;     /* minimum spin (radians/sec, 0 = no spin) */
    f32 angular_velocity_max;     /* maximum spin */
} ParticleEmitter;

/* ---- API (stateless, caller owns all memory) ---- */

/* Spawn particles in a 360-degree circular burst from an emitter config.
 * Appends to particles[current_count..], clamped to max_capacity.
 * Returns the number of particles actually emitted. */
i32 particles_emit(const ParticleEmitter *emitter,
                   Particle *particles, i32 current_count, i32 max_capacity);

/* Tick all particles: move position, rotate, decrement lifetime, swap-remove dead.
 * Returns the new particle count after dead particles are removed. */
i32 particles_update(Particle *particles, i32 count, f32 delta_time);

/* Convert live particles to InstanceData for rendering.
 * Applies color fade (linear) and scale shrink (quadratic) based on lifetime ratio.
 * Returns the number of instances written (min of count and max_instances). */
i32 particles_to_instances(const Particle *particles, i32 count,
                           InstanceData *out_instances, i32 max_instances);

#endif /* ENGINE_PARTICLES_H */
