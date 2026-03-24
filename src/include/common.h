/**
 * @file common.h
 * @brief TinyRTC common internal header - types, macros, utilities
 *
 * This header contains all internal common definitions used by the implementation.
 * All modules should include this header before other internal headers.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_COMMON_H
#define TINYRTC_COMMON_H

#include "tinyrtc/tinyrtc.h"
#include "api/aosl.h"
#include "api/aosl_types.h"
#include "api/aosl_log.h"
#include "api/aosl_mm.h"
#include "api/aosl_time.h"
#include "api/aosl_utils.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Macros
 * ========================================================================== */

/**
 * Get number of elements in a static array
 */
#define TINYRTC_ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 * Mark unused parameter to avoid compiler warnings
 */
#define TINYRTC_UNUSED(x) (void)(x)

/**
 * Min/max macros
 */
#define TINYRTC_MIN(a, b) ((a) < (b) ? (a) : (b))
#define TINYRTC_MAX(a, b) ((a) > (b) ? (a) : (b))

/**
 * Container of macro - for linked list usage
 */
#define TINYRTC_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* =============================================================================
 * Internal logging macros
 * ========================================================================== */

#if TINYRTC_DEBUG
#define TINYRTC_LOG_DEBUG(fmt, ...) aosl_log(AOSL_LOG_DEBUG, "TinyRTC: " fmt, ##__VA_ARGS__)
#else
#define TINYRTC_LOG_DEBUG(fmt, ...) do {} while(0)
#endif

#define TINYRTC_LOG_INFO(fmt, ...)  aosl_log(AOSL_LOG_INFO, "TinyRTC: " fmt, ##__VA_ARGS__)
#define TINYRTC_LOG_WARN(fmt, ...)  aosl_log(AOSL_LOG_WARNING, "TinyRTC: " fmt, ##__VA_ARGS__)
#define TINYRTC_LOG_ERROR(fmt, ...) aosl_log(AOSL_LOG_ERROR, "TinyRTC: " fmt, ##__VA_ARGS__)

/* =============================================================================
 * Memory management - wrappers around AOSL
 * ========================================================================== */

/**
 * Allocate memory - wrapper around aosl_malloc
 *
 * @param size Size in bytes to allocate
 * @return Allocated memory, NULL on failure
 */
static inline void *tinyrtc_malloc(size_t size)
{
    return aosl_malloc(size);
}

/**
 * Allocate zero-initialized memory - wrapper around aosl_calloc
 *
 * @param nmemb Number of elements
 * @param size Size of each element
 * @return Allocated memory, NULL on failure
 */
static inline void *tinyrtc_calloc(size_t nmemb, size_t size)
{
    return aosl_calloc(nmemb, size);
}

/**
 * Free memory - internal wrapper around aosl_free
 * Use this for internal allocations. The public API tinyrtc_free() is
 * implemented in src/tinyrtc.c for freeing memory allocated by TinyRTC.
 *
 * @param ptr Pointer to free, NULL is safe
 */
static inline void tinyrtc_internal_free(void *ptr)
{
    if (ptr != NULL) {
        aosl_free(ptr);
    }
}

/**
 * Duplicate a string
 *
 * @param str String to duplicate
 * @return Newly allocated copy, NULL on failure
 */
static inline char *tinyrtc_strdup(const char *str)
{
    size_t len = strlen(str) + 1;
    char *dup = (char *)tinyrtc_malloc(len);
    if (dup != NULL) {
        memcpy(dup, str, len);
    }
    return dup;
}

/* =============================================================================
 * Error handling macros
 * ========================================================================== */

/**
 * Return error if condition is false
 */
#define TINYRTC_CHECK(cond, err_code) \
    do { \
        if (!(cond)) { \
            TINYRTC_LOG_ERROR("Check failed: %s", #cond); \
            return (err_code); \
        } \
    } while (0)

/**
 * Go to label if condition is false
 */
#define TINYRTC_CHECK_GOTO(cond, label, err_code) \
    do { \
        if (!(cond)) { \
            TINYRTC_LOG_ERROR("Check failed: %s", #cond); \
            error = (err_code); \
            goto label; \
        } \
    } while (0)

/**
 * Return NULL if pointer is NULL (for functions returning pointer)
 */
#define TINYRTC_CHECK_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            TINYRTC_LOG_ERROR("NULL pointer: %s", #ptr); \
            return NULL; \
        } \
    } while (0)

/**
 * Return error code if pointer is NULL (for functions returning int)
 */
#define TINYRTC_CHECK_NULL_RET(err_code, ptr) \
    do { \
        if ((ptr) == NULL) { \
            TINYRTC_LOG_ERROR("NULL pointer: %s", #ptr); \
            return (err_code); \
        } \
    } while (0)

/* =============================================================================
 * Forward declarations
 * ========================================================================== */

struct tinyrtc_context {
    tinyrtc_config_t config;
    tinyrtc_log_level_t log_level;
    aosl_lock_t mutex;      /* For thread safety when accessing global state */
    int num_peers;          /* Number of active peer connections */
    int peers_alloc;         /* Allocated size of peers array */
    tinyrtc_peer_connection_t **peers; /* Array of peer connection pointers */
    tinyrtc_signaling_t *signaling;  /* Global signaling client (if used) */
};

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_COMMON_H */
