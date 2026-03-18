/**
 * @file tinyrtc.c
 * @brief TinyRTC core implementation - SDK initialization and main entry point
 *
 * This implements the core public API for SDK initialization and management.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/signaling.h"

/* =============================================================================
 * TinyRTC context structure - definition now in src/include/common.h
 * ========================================================================== */

/* =============================================================================
 * Static data - error code strings
 * ========================================================================== */

static const char *error_strings[] = {
    [TINYRTC_OK] = "Success",
    [-TINYRTC_ERROR] = "Generic error",
    [-TINYRTC_ERROR_INVALID_ARG] = "Invalid argument",
    [-TINYRTC_ERROR_MEMORY] = "Memory allocation failed",
    [-TINYRTC_ERROR_NETWORK] = "Network error",
    [-TINYRTC_ERROR_NOT_FOUND] = "Resource not found",
    [-TINYRTC_ERROR_IN_PROGRESS] = "Operation in progress",
    [-TINYRTC_ERROR_TIMEOUT] = "Operation timed out",
    [-TINYRTC_ERROR_INVALID_STATE] = "Invalid state for operation",
    [-TINYRTC_ERROR_DTLS_FAILED] = "DTLS handshake failed",
    [-TINYRTC_ERROR_ICE_FAILED] = "ICE negotiation failed",
    [-TINYRTC_ERROR_SIGNALING] = "Signaling connection failed",
};

static const char *log_level_names[] = {
    [TINYRTC_LOG_DEBUG] = "DEBUG",
    [TINYRTC_LOG_INFO] = "INFO",
    [TINYRTC_LOG_WARN] = "WARN",
    [TINYRTC_LOG_ERROR] = "ERROR",
};

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

void tinyrtc_get_default_config(tinyrtc_config_t *config)
{
    if (config == NULL) {
        return;
    }

    config->user_data = NULL;
    config->enable_debug_log = (bool)(TINYRTC_DEBUG);
    config->max_peer_connections = TINYRTC_MAX_PEERS;
}

tinyrtc_context_t *tinyrtc_init(const tinyrtc_config_t *config)
{
    if (config == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_init: config is NULL");
        return NULL;
    }

    tinyrtc_context_t *ctx = (tinyrtc_context_t *)tinyrtc_calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_init: memory allocation failed");
        return NULL;
    }

    /* Copy configuration */
    ctx->config = *config;

    /* Set default log level based on debug setting */
    if (config->enable_debug_log) {
        ctx->log_level = TINYRTC_LOG_DEBUG;
    } else {
        ctx->log_level = TINYRTC_LOG_INFO;
    }

    /* Initialize mutex for thread safety */
    ctx->mutex = aosl_lock_create();
    if (ctx->mutex == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_init: lock creation failed");
        tinyrtc_internal_free(ctx);
        return NULL;
    }

    ctx->num_peers = 0;

    TINYRTC_LOG_INFO("TinyRTC initialized version %d.%d.%d",
        TINYRTC_VERSION_MAJOR, TINYRTC_VERSION_MINOR, TINYRTC_VERSION_PATCH);

    return ctx;
}

void tinyrtc_destroy(tinyrtc_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* TODO: Destroy all peer connections before freeing context */
    /* For now, just check and warn if there are active peers */
    if (ctx->num_peers > 0) {
        TINYRTC_LOG_WARN("tinyrtc_destroy: %d active peers still exist", ctx->num_peers);
    }

    if (ctx->mutex != NULL) {
        aosl_lock_destroy(ctx->mutex);
    }

    TINYRTC_LOG_INFO("TinyRTC destroyed");
    tinyrtc_internal_free(ctx);
}

int tinyrtc_process_events(tinyrtc_context_t *ctx, uint32_t timeout_ms)
{
    TINYRTC_CHECK_NULL_RET(0, ctx);
    (void)timeout_ms;

    int events_processed = 0;

    /* Lock the context for thread safety */
    aosl_lock_lock(ctx->mutex);

    /* Process signaling messages */
    if (ctx->signaling) {
        int ret = tinyrtc_signaling_process(ctx->signaling);
        if (ret > 0) {
            events_processed += ret;
        }
    }

    /* TODO:
     * 1. Process network events (socket polling)
     * 2. Process timers (timeouts, retransmissions)
     * 3. Dispatch callbacks to application
     * 4. Process any pending events in each peer connection
     */

    aosl_lock_unlock(ctx->mutex);

    return events_processed;
}

const char *tinyrtc_get_error_string(tinyrtc_error_t error)
{
    int idx = -error;
    if (idx < 0 || idx >= (int)TINYRTC_ARRAY_LEN(error_strings)) {
        return "Unknown error";
    }
    return error_strings[idx];
}

void tinyrtc_set_log_level(tinyrtc_context_t *ctx, tinyrtc_log_level_t level)
{
    if (ctx == NULL) {
        return;
    }
    if (level < TINYRTC_LOG_DEBUG || level > TINYRTC_LOG_ERROR) {
        return;
    }
    ctx->log_level = level;

    /* Map to AOSL log level and set global level */
    /* AOSL logging is already integrated via our TINYRTC_LOG_* macros */
    TINYRTC_LOG_DEBUG("Log level set to %s", log_level_names[level]);
}

void tinyrtc_free(void *ptr)
{
    /* Note: This is the public API free for memory allocated by TinyRTC
     * (like SDP strings). Our internal tinyrtc_free is a macro that wraps
     * aosl_free. We need to keep the name for the public API. */
    if (ptr != NULL) {
        aosl_free(ptr);
    }
}
