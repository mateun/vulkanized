#ifndef ENGINE_COMMON_H
#define ENGINE_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Basic type aliases ---- */
typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef uint64_t  u64;
typedef int8_t    i8;
typedef int16_t   i16;
typedef int32_t   i32;
typedef int64_t   i64;
typedef float     f32;
typedef double    f64;

/* ---- Engine-wide result codes ---- */
typedef enum {
    ENGINE_SUCCESS = 0,
    ENGINE_ERROR_GENERIC,
    ENGINE_ERROR_OUT_OF_MEMORY,
    ENGINE_ERROR_FILE_NOT_FOUND,
    ENGINE_ERROR_VULKAN_INIT,
    ENGINE_ERROR_VULKAN_DEVICE,
    ENGINE_ERROR_VULKAN_SWAPCHAIN,
    ENGINE_ERROR_VULKAN_PIPELINE,
    ENGINE_ERROR_VULKAN_SURFACE,
    ENGINE_ERROR_WINDOW_INIT,
} EngineResult;

/* ---- Utility macros ---- */
#define ENGINE_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

#define ENGINE_CLAMP(val, lo, hi) \
    ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))

#define ENGINE_MAX(a, b) ((a) > (b) ? (a) : (b))
#define ENGINE_MIN(a, b) ((a) < (b) ? (a) : (b))

/* ---- Unused parameter suppression ---- */
#define ENGINE_UNUSED(x) (void)(x)

#endif /* ENGINE_COMMON_H */
