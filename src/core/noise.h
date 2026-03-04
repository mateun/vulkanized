#ifndef ENGINE_NOISE_H
#define ENGINE_NOISE_H

#include "core/common.h"

/* Classic 2D Perlin noise — returns value in [-1, 1] */
f32 noise_perlin2d(f32 x, f32 y);

/* Fractal Brownian motion (layered Perlin noise) — returns value in approx [-1, 1]
 * octaves:    number of noise layers (e.g. 4-8)
 * lacunarity: frequency multiplier per octave (typically 2.0)
 * gain:       amplitude multiplier per octave (typically 0.5) */
f32 noise_fbm2d(f32 x, f32 y, i32 octaves, f32 lacunarity, f32 gain);

#endif /* ENGINE_NOISE_H */
