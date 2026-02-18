#ifndef ENGINE_PRIMITIVES_H
#define ENGINE_PRIMITIVES_H

#include "core/common.h"
#include "renderer/renderer_types.h"

/* Forward decl */
typedef struct Renderer Renderer;

/* Procedural 3D primitives — centered at origin, unit-sized (-0.5 to 0.5).
 * All faces have outward normals and counter-clockwise winding.
 * Vertex color is white (1,1,1). UVs cover 0-1 per face. */

EngineResult renderer_create_cube(Renderer *renderer, MeshHandle *out_handle);

EngineResult renderer_create_sphere(Renderer *renderer, u32 segments, u32 rings,
                                    MeshHandle *out_handle);

EngineResult renderer_create_cylinder(Renderer *renderer, u32 segments,
                                      MeshHandle *out_handle);

#endif /* ENGINE_PRIMITIVES_H */
