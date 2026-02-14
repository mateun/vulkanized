#ifndef ENGINE_ARENA_H
#define ENGINE_ARENA_H

#include "core/common.h"

/* Simple linear (bump) allocator. Allocates from a fixed-size block.
 * Free all at once with arena_reset(). No individual frees. */

typedef struct {
    u8    *buf;
    size_t capacity;
    size_t offset;
} Arena;

/* Initialize arena with a pre-allocated buffer. */
void   arena_init(Arena *arena, void *buf, size_t capacity);

/* Allocate `size` bytes aligned to `align`. Returns NULL if out of space. */
void  *arena_alloc(Arena *arena, size_t size, size_t align);

/* Reset arena to empty (does not free the backing buffer). */
void   arena_reset(Arena *arena);

/* Convenience: allocate with default alignment. */
#define arena_push(arena, type) \
    ((type *)arena_alloc((arena), sizeof(type), _Alignof(type)))

#define arena_push_array(arena, type, count) \
    ((type *)arena_alloc((arena), sizeof(type) * (count), _Alignof(type)))

#endif /* ENGINE_ARENA_H */
