/**
 * @file tinyrtc.h
 * @brief TinyRTC main public header - Core SDK initialization
 *
 * This is the only public header that users need to include directly.
 * All other public APIs are included from here.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_H
#define TINYRTC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TinyRTC version information
 */
#define TINYRTC_VERSION_MAJOR 0
#define TINYRTC_VERSION_MINOR 1
#define TINYRTC_VERSION_PATCH 0

/**
 * @brief Error codes returned by TinyRTC functions
 */
typedef enum {
    TINYRTC_OK = 0,                  /**< Operation succeeded */
    TINYRTC_ERROR = -1,             /**< Generic error */
    TINYRTC_ERROR_INVALID_ARG = -2, /**< Invalid argument */
    TINYRTC_ERROR_MEMORY = -3,       /**< Memory allocation failed */
    TINYRTC_ERROR_NETWORK = -4,       /**< Network error */
    TINYRTC_ERROR_NOT_FOUND = -5,   /**< Resource not found */
    TINYRTC_ERROR_IN_PROGRESS = -6,/**< Operation still in progress */
    TINYRTC_ERROR_TIMEOUT = -7,       /**< Operation timed out */
    TINYRTC_ERROR_INVALID_STATE = -8,/**< Invalid state for operation */
    TINYRTC_ERROR_DTLS_FAILED = -9,  /**< DTLS handshake failed */
    TINYRTC_ERROR_ICE_FAILED = -10, /**< ICE negotiation failed */
    TINYRTC_ERROR_SIGNALING = -11,  /**< Signaling connection failed */
} tinyrtc_error_t;

/**
 * @brief Log level definitions
 */
typedef enum {
    TINYRTC_LOG_DEBUG = 0,
    TINYRTC_LOG_INFO = 1,
    TINYRTC_LOG_WARN = 2,
    TINYRTC_LOG_ERROR = 3,
} tinyrtc_log_level_t;

/**
 * @brief TinyRTC global configuration
 */
typedef struct {
    void *user_data;                  /**< Global user context passed to all callbacks */
    bool enable_debug_log;            /**< Enable debug logging */
    uint32_t max_peer_connections;   /**< Maximum number of concurrent peer connections */
} tinyrtc_config_t;

/**
 * @brief TinyRTC global context
 */
typedef struct tinyrtc_context tinyrtc_context_t;

/* Note: peer_connection must come before signaling because signaling uses tinyrtc_ice_candidate_t */
#include "tinyrtc/media_codec.h"
#include "tinyrtc/peer_connection.h"
#include "tinyrtc/signaling.h"

/**
 * @brief Get default configuration
 *
 * @param config Output configuration structure to be filled with defaults
 */
void tinyrtc_get_default_config(tinyrtc_config_t *config);

/**
 * @brief Initialize TinyRTC SDK global context
 *
 * @param config Configuration structure
 * @return New context, NULL on error
 */
tinyrtc_context_t *tinyrtc_init(const tinyrtc_config_t *config);

/**
 * @brief Destroy TinyRTC SDK global context
 *
 * @param ctx Context to destroy
 */
void tinyrtc_destroy(tinyrtc_context_t *ctx);

/**
 * @brief Process pending events (must be called periodically in single-threaded mode)
 *
 * In single-threaded operation, this should be called every ~10ms to process
 * network events, timeouts, and trigger callbacks.
 *
 * @param ctx TinyRTC context
 * @param timeout_ms Maximum time to wait for events in milliseconds
 * @return Number of events processed
 */
int tinyrtc_process_events(tinyrtc_context_t *ctx, uint32_t timeout_ms);

/**
 * @brief Get human-readable error string for error code
 *
 * @param error Error code
 * @return Human-readable error string
 */
const char *tinyrtc_get_error_string(tinyrtc_error_t error);

/**
 * @brief Set global log level
 *
 * @param ctx TinyRTC context
 * @param level Log level
 */
void tinyrtc_set_log_level(tinyrtc_context_t *ctx, tinyrtc_log_level_t level);

/**
 * @brief Free memory allocated by TinyRTC (e.g., SDP strings)
 *
 * @param ptr Pointer to free
 */
void tinyrtc_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_H */
