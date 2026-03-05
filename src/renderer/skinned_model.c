#include "renderer/skinned_model.h"
#include "renderer/vk_types.h"
#include "renderer/vk_buffer.h"
#include "renderer/animation_types.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <cglm/mat4.h>
#include <cglm/quat.h>
#include <cglm/affine.h>

/* cgltf is already implemented in model.c — just include the header here */
#include "cgltf/cgltf.h"

/* --------------------------------------------------------------------------
 * Helper: find the local joint index for a cgltf_node* within a skin
 * ------------------------------------------------------------------------ */

static i32 find_joint_index(cgltf_node **joints, cgltf_size count, cgltf_node *node) {
    for (cgltf_size i = 0; i < count; i++) {
        if (joints[i] == node) return (i32)i;
    }
    return -1;
}

/* --------------------------------------------------------------------------
 * Compute a node's local transform as a mat4 (T * R * S)
 * ------------------------------------------------------------------------ */

static void node_local_transform(cgltf_node *node, mat4 out) {
    if (node->has_matrix) {
        memcpy(out, node->matrix, sizeof(mat4));
        return;
    }

    mat4 T, R, S, TR;
    glm_mat4_identity(T);
    glm_mat4_identity(S);

    if (node->has_translation)
        glm_translate(T, node->translation);

    if (node->has_rotation)
        glm_quat_mat4(node->rotation, R);  /* cgltf: [x,y,z,w] matches cglm */
    else
        glm_mat4_identity(R);

    if (node->has_scale)
        glm_scale(S, node->scale);

    glm_mat4_mul(T, R, TR);
    glm_mat4_mul(TR, S, out);
}

/* --------------------------------------------------------------------------
 * Compute the world transform of a node by walking up the parent chain
 * ------------------------------------------------------------------------ */

static void node_world_transform(cgltf_node *node, mat4 out) {
    if (!node) {
        glm_mat4_identity(out);
        return;
    }

    mat4 local;
    node_local_transform(node, local);

    if (node->parent) {
        mat4 parent_world;
        node_world_transform(node->parent, parent_world);
        glm_mat4_mul(parent_world, local, out);
    } else {
        glm_mat4_copy(local, out);
    }
}

/* --------------------------------------------------------------------------
 * Extract skeleton from cgltf_skin
 * ------------------------------------------------------------------------ */

static void extract_skeleton(cgltf_skin *skin, Skeleton *skel) {
    skel->joint_count = (u32)skin->joints_count;
    if (skel->joint_count > MAX_JOINTS) {
        LOG_WARN("Model has %u joints, clamping to %d", skel->joint_count, MAX_JOINTS);
        skel->joint_count = MAX_JOINTS;
    }

    /* Parent indices */
    for (u32 j = 0; j < skel->joint_count; j++) {
        cgltf_node *node = skin->joints[j];
        if (node->parent) {
            skel->parent_indices[j] = find_joint_index(skin->joints, skin->joints_count,
                                                         node->parent);
        } else {
            skel->parent_indices[j] = -1;
        }
    }

    /* Inverse bind matrices */
    if (skin->inverse_bind_matrices) {
        for (u32 j = 0; j < skel->joint_count; j++) {
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j,
                                       skel->inverse_bind_matrices[j], 16);
        }
    } else {
        /* No inverse bind matrices: use identity */
        for (u32 j = 0; j < skel->joint_count; j++) {
            memset(skel->inverse_bind_matrices[j], 0, sizeof(f32) * 16);
            skel->inverse_bind_matrices[j][0]  = 1.0f;
            skel->inverse_bind_matrices[j][5]  = 1.0f;
            skel->inverse_bind_matrices[j][10] = 1.0f;
            skel->inverse_bind_matrices[j][15] = 1.0f;
        }
    }

    /* Rest pose TRS from each joint node */
    for (u32 j = 0; j < skel->joint_count; j++) {
        cgltf_node *node = skin->joints[j];

        if (node->has_translation) {
            memcpy(skel->rest_translations[j], node->translation, sizeof(f32) * 3);
        } else {
            skel->rest_translations[j][0] = 0.0f;
            skel->rest_translations[j][1] = 0.0f;
            skel->rest_translations[j][2] = 0.0f;
        }

        if (node->has_rotation) {
            /* cgltf stores quaternion as [x, y, z, w] — matches cglm */
            memcpy(skel->rest_rotations[j], node->rotation, sizeof(f32) * 4);
        } else {
            skel->rest_rotations[j][0] = 0.0f;
            skel->rest_rotations[j][1] = 0.0f;
            skel->rest_rotations[j][2] = 0.0f;
            skel->rest_rotations[j][3] = 1.0f; /* identity quaternion */
        }

        if (node->has_scale) {
            memcpy(skel->rest_scales[j], node->scale, sizeof(f32) * 3);
        } else {
            skel->rest_scales[j][0] = 1.0f;
            skel->rest_scales[j][1] = 1.0f;
            skel->rest_scales[j][2] = 1.0f;
        }
    }

    /* Compute skeleton root transform.
     * glTF skins have a 'skeleton' property pointing to the root node of the
     * joint hierarchy. Any transforms on nodes *above* the joints (i.e. the
     * skeleton node's parent chain) need to be applied to root joints so the
     * model isn't rotated incorrectly.
     *
     * We compute the world transform of each root joint's parent (if it exists
     * but isn't itself a joint). If there's no such parent, we use identity. */
    glm_mat4_identity((vec4 *)skel->root_transform);

    /* Use the skin's skeleton root node if available */
    cgltf_node *skel_root = skin->skeleton;
    if (skel_root) {
        /* The skeleton root node may itself be a joint or a parent of joints.
         * We need the world transform of this node to orient the skeleton. */
        i32 skel_root_ji = find_joint_index(skin->joints, skin->joints_count, skel_root);
        if (skel_root_ji < 0) {
            /* Skeleton root is NOT a joint — its world transform is the root transform */
            node_world_transform(skel_root, (vec4 *)skel->root_transform);
        } else {
            /* Skeleton root IS a joint — use its parent's world transform if it has one */
            if (skel_root->parent) {
                node_world_transform(skel_root->parent, (vec4 *)skel->root_transform);
            }
        }
    } else {
        /* No skeleton root specified — find the first root joint and use its parent */
        for (u32 j = 0; j < skel->joint_count; j++) {
            if (skel->parent_indices[j] < 0 && skin->joints[j]->parent) {
                node_world_transform(skin->joints[j]->parent, (vec4 *)skel->root_transform);
                break;
            }
        }
    }
}

/* --------------------------------------------------------------------------
 * Extract animation clips from cgltf_data
 * ------------------------------------------------------------------------ */

static void extract_animations(cgltf_data *data, cgltf_skin *skin,
                                AnimClip **out_clips, u32 *out_clip_count) {
    if (data->animations_count == 0) {
        *out_clips = NULL;
        *out_clip_count = 0;
        return;
    }

    *out_clip_count = (u32)data->animations_count;
    *out_clips = calloc(*out_clip_count, sizeof(AnimClip));
    if (!*out_clips) {
        LOG_ERROR("Out of memory allocating animation clips");
        *out_clip_count = 0;
        return;
    }

    for (u32 a = 0; a < *out_clip_count; a++) {
        cgltf_animation *anim = &data->animations[a];
        AnimClip *clip = &(*out_clips)[a];

        /* Name */
        if (anim->name) {
            strncpy(clip->name, anim->name, sizeof(clip->name) - 1);
        } else {
            snprintf(clip->name, sizeof(clip->name), "anim_%u", a);
        }

        /* Count valid channels (ones targeting joints in our skin) */
        u32 valid_count = 0;
        for (cgltf_size ci = 0; ci < anim->channels_count; ci++) {
            cgltf_animation_channel *ch = &anim->channels[ci];
            if (!ch->target_node) continue;
            if (ch->target_path == cgltf_animation_path_type_weights) continue;
            i32 ji = find_joint_index(skin->joints, skin->joints_count, ch->target_node);
            if (ji >= 0) valid_count++;
        }

        clip->channel_count = valid_count;
        clip->channels = calloc(valid_count, sizeof(AnimChannel));
        if (!clip->channels) {
            LOG_ERROR("Out of memory allocating animation channels");
            clip->channel_count = 0;
            continue;
        }

        clip->duration = 0.0f;
        u32 out_ci = 0;

        for (cgltf_size ci = 0; ci < anim->channels_count; ci++) {
            cgltf_animation_channel *gltf_ch = &anim->channels[ci];
            if (!gltf_ch->target_node) continue;
            if (gltf_ch->target_path == cgltf_animation_path_type_weights) continue;

            i32 joint_idx = find_joint_index(skin->joints, skin->joints_count,
                                              gltf_ch->target_node);
            if (joint_idx < 0) continue;

            cgltf_animation_sampler *sampler = gltf_ch->sampler;
            AnimChannel *ch = &clip->channels[out_ci++];

            ch->target_joint = (u32)joint_idx;

            /* Path type */
            switch (gltf_ch->target_path) {
            case cgltf_animation_path_type_translation:
                ch->path = ANIM_PATH_TRANSLATION;
                break;
            case cgltf_animation_path_type_rotation:
                ch->path = ANIM_PATH_ROTATION;
                break;
            case cgltf_animation_path_type_scale:
                ch->path = ANIM_PATH_SCALE;
                break;
            default:
                continue;
            }

            /* Interpolation */
            switch (sampler->interpolation) {
            case cgltf_interpolation_type_step:
                ch->interpolation = ANIM_INTERP_STEP;
                break;
            case cgltf_interpolation_type_linear:
                ch->interpolation = ANIM_INTERP_LINEAR;
                break;
            case cgltf_interpolation_type_cubic_spline:
                ch->interpolation = ANIM_INTERP_CUBICSPLINE;
                break;
            default:
                ch->interpolation = ANIM_INTERP_LINEAR;
                break;
            }

            /* Timestamps (input accessor) */
            ch->keyframe_count = (u32)sampler->input->count;
            ch->timestamps = malloc(sizeof(f32) * ch->keyframe_count);
            if (ch->timestamps) {
                for (u32 k = 0; k < ch->keyframe_count; k++) {
                    cgltf_accessor_read_float(sampler->input, k, &ch->timestamps[k], 1);
                }
                /* Track max timestamp for clip duration */
                if (ch->keyframe_count > 0) {
                    f32 last_t = ch->timestamps[ch->keyframe_count - 1];
                    if (last_t > clip->duration) clip->duration = last_t;
                }
            }

            /* Values (output accessor) */
            u32 components = (ch->path == ANIM_PATH_ROTATION) ? 4 : 3;
            u32 value_count = (u32)sampler->output->count;

            /* CubicSpline has 3x the values (in-tangent, value, out-tangent) */
            u32 total_floats = value_count * components;
            ch->values = malloc(sizeof(f32) * total_floats);
            if (ch->values) {
                for (u32 k = 0; k < value_count; k++) {
                    cgltf_accessor_read_float(sampler->output, k,
                                               ch->values + k * components,
                                               components);
                }
            }
        }
    }
}

/* Internal helper to free just clip data (used on error paths) */
static void skinned_model_destroy_clips(SkinnedModel *model) {
    if (!model) return;
    for (u32 c = 0; c < model->clip_count; c++) {
        AnimClip *clip = &model->clips[c];
        for (u32 ch = 0; ch < clip->channel_count; ch++) {
            free(clip->channels[ch].timestamps);
            free(clip->channels[ch].values);
        }
        free(clip->channels);
    }
    free(model->clips);
    model->clips = NULL;
    model->clip_count = 0;
}

/* --------------------------------------------------------------------------
 * renderer_load_skinned_model
 * ------------------------------------------------------------------------ */

EngineResult renderer_load_skinned_model(VulkanContext *vk, const char *path,
                                          SkinnedModel *out_model) {
    if (!vk || !path || !out_model) {
        LOG_ERROR("renderer_load_skinned_model: NULL argument");
        return ENGINE_ERROR_GENERIC;
    }

    memset(out_model, 0, sizeof(*out_model));

    /* ---- Parse glTF ---- */
    cgltf_options options = {0};
    cgltf_data *data = NULL;
    cgltf_result result = cgltf_parse_file(&options, path, &data);

    if (result != cgltf_result_success) {
        LOG_ERROR("Failed to parse glTF file: %s (error %d)", path, (int)result);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        LOG_ERROR("Failed to load glTF buffers: %s (error %d)", path, (int)result);
        cgltf_free(data);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    /* ---- Require at least one skin ---- */
    if (data->skins_count == 0) {
        LOG_ERROR("No skins found in glTF: %s", path);
        cgltf_free(data);
        return ENGINE_ERROR_GENERIC;
    }

    cgltf_skin *skin = &data->skins[0];

    /* ---- Extract skeleton ---- */
    extract_skeleton(skin, &out_model->skeleton);

    /* ---- Extract animations ---- */
    extract_animations(data, skin, &out_model->clips, &out_model->clip_count);

    /* ---- Count vertices and indices (first pass) ---- */
    u32 total_verts = 0;
    u32 total_indices = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive *prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor *pos_acc = NULL;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    pos_acc = prim->attributes[ai].data;
                    break;
                }
            }
            if (!pos_acc) continue;

            total_verts += (u32)pos_acc->count;
            total_indices += prim->indices ? (u32)prim->indices->count : (u32)pos_acc->count;
        }
    }

    if (total_verts == 0) {
        LOG_ERROR("No valid geometry found in skinned glTF: %s", path);
        cgltf_free(data);
        return ENGINE_ERROR_GENERIC;
    }

    /* ---- Allocate temp arrays ---- */
    SkinnedVertex3D *vertices = malloc(sizeof(SkinnedVertex3D) * total_verts);
    u32 *indices = malloc(sizeof(u32) * total_indices);

    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        cgltf_free(data);
        LOG_ERROR("Out of memory loading skinned model: %s", path);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    /* ---- Extract vertex + index data (second pass) ---- */
    u32 vert_cursor = 0;
    u32 idx_cursor = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive *prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            /* Find attribute accessors */
            cgltf_accessor *pos_acc = NULL;
            cgltf_accessor *norm_acc = NULL;
            cgltf_accessor *uv_acc = NULL;
            cgltf_accessor *joints_acc = NULL;
            cgltf_accessor *weights_acc = NULL;

            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                switch (prim->attributes[ai].type) {
                case cgltf_attribute_type_position:
                    pos_acc = prim->attributes[ai].data;
                    break;
                case cgltf_attribute_type_normal:
                    norm_acc = prim->attributes[ai].data;
                    break;
                case cgltf_attribute_type_texcoord:
                    if (prim->attributes[ai].index == 0)
                        uv_acc = prim->attributes[ai].data;
                    break;
                case cgltf_attribute_type_joints:
                    if (prim->attributes[ai].index == 0)
                        joints_acc = prim->attributes[ai].data;
                    break;
                case cgltf_attribute_type_weights:
                    if (prim->attributes[ai].index == 0)
                        weights_acc = prim->attributes[ai].data;
                    break;
                default:
                    break;
                }
            }
            if (!pos_acc) continue;

            u32 prim_vert_count = (u32)pos_acc->count;
            u32 vertex_base = vert_cursor;

            /* Extract vertices */
            for (u32 v = 0; v < prim_vert_count; v++) {
                SkinnedVertex3D *vert = &vertices[vert_cursor + v];

                /* Position */
                cgltf_accessor_read_float(pos_acc, v, vert->position, 3);

                /* Normal */
                if (norm_acc) {
                    cgltf_accessor_read_float(norm_acc, v, vert->normal, 3);
                } else {
                    vert->normal[0] = 0.0f;
                    vert->normal[1] = 1.0f;
                    vert->normal[2] = 0.0f;
                }

                /* UV */
                if (uv_acc) {
                    cgltf_accessor_read_float(uv_acc, v, vert->uv, 2);
                } else {
                    vert->uv[0] = 0.0f;
                    vert->uv[1] = 0.0f;
                }

                /* Color: white (tinted by instance color) */
                vert->color[0] = 1.0f;
                vert->color[1] = 1.0f;
                vert->color[2] = 1.0f;

                /* Joints — read as float then cast to u32
                 * glTF JOINTS_0 values are indices into skin.joints[] */
                if (joints_acc) {
                    f32 jf[4];
                    cgltf_accessor_read_float(joints_acc, v, jf, 4);
                    vert->joints[0] = (u32)jf[0];
                    vert->joints[1] = (u32)jf[1];
                    vert->joints[2] = (u32)jf[2];
                    vert->joints[3] = (u32)jf[3];
                } else {
                    vert->joints[0] = 0;
                    vert->joints[1] = 0;
                    vert->joints[2] = 0;
                    vert->joints[3] = 0;
                }

                /* Weights */
                if (weights_acc) {
                    cgltf_accessor_read_float(weights_acc, v, vert->weights, 4);
                } else {
                    vert->weights[0] = 1.0f;
                    vert->weights[1] = 0.0f;
                    vert->weights[2] = 0.0f;
                    vert->weights[3] = 0.0f;
                }
            }
            vert_cursor += prim_vert_count;

            /* Extract indices */
            if (prim->indices) {
                cgltf_accessor *idx_acc = prim->indices;
                for (u32 i = 0; i < (u32)idx_acc->count; i++) {
                    indices[idx_cursor + i] =
                        (u32)cgltf_accessor_read_index(idx_acc, i) + vertex_base;
                }
                idx_cursor += (u32)idx_acc->count;
            } else {
                for (u32 i = 0; i < prim_vert_count; i++) {
                    indices[idx_cursor + i] = vertex_base + i;
                }
                idx_cursor += prim_vert_count;
            }
        }
    }

    /* ---- Upload to GPU ---- */
    MeshHandle mesh_handle;
    EngineResult res = vk_upload_mesh_skinned(vk, vertices, vert_cursor,
                                               indices, idx_cursor, &mesh_handle);

    free(vertices);
    free(indices);
    cgltf_free(data);

    if (res != ENGINE_SUCCESS) {
        skinned_model_destroy_clips(out_model);
        return res;
    }

    out_model->mesh_handle = mesh_handle;

    LOG_INFO("Skinned model loaded: %s (%u verts, %u indices, %u joints, %u clips)",
             path, vert_cursor, idx_cursor,
             out_model->skeleton.joint_count, out_model->clip_count);

    return ENGINE_SUCCESS;
}
