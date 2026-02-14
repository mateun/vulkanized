#include "core/arena.h"
#include <string.h>

void arena_init(Arena *arena, void *buf, size_t capacity) {
    arena->buf      = (u8 *)buf;
    arena->capacity = capacity;
    arena->offset   = 0;
}

void *arena_alloc(Arena *arena, size_t size, size_t align) {
    /* Align the current offset upward */
    size_t aligned = (arena->offset + (align - 1)) & ~(align - 1);

    if (aligned + size > arena->capacity) {
        return NULL; /* out of space */
    }

    void *ptr = arena->buf + aligned;
    arena->offset = aligned + size;
    memset(ptr, 0, size);
    return ptr;
}

void arena_reset(Arena *arena) {
    arena->offset = 0;
}
