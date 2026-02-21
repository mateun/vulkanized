#ifndef ENGINE_SKINNED_MODEL_H
#define ENGINE_SKINNED_MODEL_H

#include "core/common.h"
#include "renderer/renderer_types.h"
#include "renderer/animation_types.h"

/* Forward decl — skinned_model.c works directly with VulkanContext */
typedef struct VulkanContext VulkanContext;

/* Load a glTF model with skeleton and animation data.
 * Extracts skinned geometry (POSITION, NORMAL, UV, JOINTS_0, WEIGHTS_0),
 * skeleton hierarchy (joint parents + inverse bind matrices),
 * and all animation clips.
 * Returns ENGINE_ERROR_GENERIC if the model has no skin.
 * NOTE: This is an internal function — games call renderer_load_skinned_model()
 * which wraps this with the public Renderer pointer. */
EngineResult renderer_load_skinned_model(VulkanContext *vk, const char *path,
                                         SkinnedModel *out_model);

#endif /* ENGINE_SKINNED_MODEL_H */
