#include "renderer/model.h"
#include "renderer/renderer.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

/* cgltf implementation — define before including the header.
 * Same pattern as stb_impl.c for stb_image. */
#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

/* --------------------------------------------------------------------------
 * renderer_load_model — load a glTF (.gltf / .glb) file and upload as a
 * single MeshHandle.  All meshes and triangle primitives are merged.
 * ------------------------------------------------------------------------ */

EngineResult renderer_load_model(Renderer *renderer, const char *path,
                                 MeshHandle *out_handle) {
    if (!renderer || !path || !out_handle) {
        LOG_ERROR("renderer_load_model: NULL argument");
        return ENGINE_ERROR_GENERIC;
    }

    /* ---- Step 1: Parse the glTF file ---- */
    cgltf_options options = {0};
    cgltf_data  *data     = NULL;
    cgltf_result result   = cgltf_parse_file(&options, path, &data);

    if (result != cgltf_result_success) {
        LOG_ERROR("Failed to parse glTF file: %s (cgltf error %d)", path, (int)result);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    /* ---- Step 2: Load binary buffer data ---- */
    result = cgltf_load_buffers(&options, data, path);
    if (result != cgltf_result_success) {
        LOG_ERROR("Failed to load glTF buffers: %s (cgltf error %d)", path, (int)result);
        cgltf_free(data);
        return ENGINE_ERROR_FILE_NOT_FOUND;
    }

    /* ---- Step 3: First pass — count total vertices and indices ---- */
    u32 total_verts   = 0;
    u32 total_indices = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive *prim = &mesh->primitives[pi];

            /* Only support triangle primitives */
            if (prim->type != cgltf_primitive_type_triangles) {
                LOG_WARN("Skipping non-triangle primitive in %s (mesh %zu, prim %zu)",
                         path, mi, pi);
                continue;
            }

            /* Find POSITION accessor to get vertex count */
            cgltf_accessor *pos_acc = NULL;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                if (prim->attributes[ai].type == cgltf_attribute_type_position) {
                    pos_acc = prim->attributes[ai].data;
                    break;
                }
            }
            if (!pos_acc) continue;

            total_verts += (u32)pos_acc->count;

            if (prim->indices) {
                total_indices += (u32)prim->indices->count;
            } else {
                /* Non-indexed: we'll generate sequential indices */
                total_indices += (u32)pos_acc->count;
            }
        }
    }

    if (total_verts == 0) {
        LOG_ERROR("No valid geometry found in glTF: %s", path);
        cgltf_free(data);
        return ENGINE_ERROR_GENERIC;
    }

    /* ---- Step 4: Allocate temporary CPU-side arrays ---- */
    Vertex3D *vertices = malloc(sizeof(Vertex3D) * total_verts);
    u32      *indices  = malloc(sizeof(u32) * total_indices);

    if (!vertices || !indices) {
        free(vertices);
        free(indices);
        cgltf_free(data);
        LOG_ERROR("Out of memory loading model: %s", path);
        return ENGINE_ERROR_OUT_OF_MEMORY;
    }

    /* ---- Step 5: Second pass — extract vertex and index data ---- */
    u32 vert_cursor = 0;
    u32 idx_cursor  = 0;

    for (cgltf_size mi = 0; mi < data->meshes_count; mi++) {
        cgltf_mesh *mesh = &data->meshes[mi];
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive *prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles) continue;

            /* Find attribute accessors */
            cgltf_accessor *pos_acc  = NULL;
            cgltf_accessor *norm_acc = NULL;
            cgltf_accessor *uv_acc   = NULL;

            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                switch (prim->attributes[ai].type) {
                    case cgltf_attribute_type_position:
                        pos_acc = prim->attributes[ai].data;
                        break;
                    case cgltf_attribute_type_normal:
                        norm_acc = prim->attributes[ai].data;
                        break;
                    case cgltf_attribute_type_texcoord:
                        /* Take TEXCOORD_0 only */
                        if (prim->attributes[ai].index == 0)
                            uv_acc = prim->attributes[ai].data;
                        break;
                    default:
                        break;
                }
            }
            if (!pos_acc) continue;

            u32 prim_vert_count = (u32)pos_acc->count;
            u32 vertex_base     = vert_cursor;

            /* Extract vertices */
            for (u32 v = 0; v < prim_vert_count; v++) {
                Vertex3D *vert = &vertices[vert_cursor + v];

                /* Position (required) */
                cgltf_accessor_read_float(pos_acc, v, vert->position, 3);

                /* Normal (optional — default to up) */
                if (norm_acc) {
                    cgltf_accessor_read_float(norm_acc, v, vert->normal, 3);
                } else {
                    vert->normal[0] = 0.0f;
                    vert->normal[1] = 1.0f;
                    vert->normal[2] = 0.0f;
                }

                /* UV (optional — default to 0,0) */
                if (uv_acc) {
                    cgltf_accessor_read_float(uv_acc, v, vert->uv, 2);
                } else {
                    vert->uv[0] = 0.0f;
                    vert->uv[1] = 0.0f;
                }

                /* Color: always white (tinted by instance color at draw time) */
                vert->color[0] = 1.0f;
                vert->color[1] = 1.0f;
                vert->color[2] = 1.0f;
            }
            vert_cursor += prim_vert_count;

            /* Extract indices — offset by vertex_base for merging */
            if (prim->indices) {
                cgltf_accessor *idx_acc = prim->indices;
                for (u32 i = 0; i < (u32)idx_acc->count; i++) {
                    indices[idx_cursor + i] =
                        (u32)cgltf_accessor_read_index(idx_acc, i) + vertex_base;
                }
                idx_cursor += (u32)idx_acc->count;
            } else {
                /* Non-indexed: generate sequential indices */
                for (u32 i = 0; i < prim_vert_count; i++) {
                    indices[idx_cursor + i] = vertex_base + i;
                }
                idx_cursor += prim_vert_count;
            }
        }
    }

    /* ---- Step 6: Upload to GPU ---- */
    EngineResult res = renderer_upload_mesh_3d(renderer, vertices, vert_cursor,
                                               indices, idx_cursor, out_handle);

    free(vertices);
    free(indices);
    cgltf_free(data);

    if (res == ENGINE_SUCCESS) {
        LOG_INFO("Model loaded: %s (%u vertices, %u indices)", path,
                 vert_cursor, idx_cursor);
    }
    return res;
}
