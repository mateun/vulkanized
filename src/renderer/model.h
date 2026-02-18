#ifndef ENGINE_MODEL_H
#define ENGINE_MODEL_H

#include "core/common.h"
#include "renderer/renderer_types.h"

typedef struct Renderer Renderer;

/* Load a glTF (.gltf or .glb) model file and upload its geometry as a single mesh.
 * All meshes/primitives in the file are merged into one MeshHandle.
 * Non-triangle primitives (lines, points) are skipped.
 * Missing normals default to (0,1,0), missing UVs default to (0,0),
 * vertex color defaults to white (1,1,1). */
EngineResult renderer_load_model(Renderer *renderer, const char *path,
                                 MeshHandle *out_handle);

#endif /* ENGINE_MODEL_H */
