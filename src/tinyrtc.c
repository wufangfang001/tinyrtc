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
#include "ice_internal.h"
#include "peer_connection_internal.h"
#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/signaling.h"

#include <sys/select.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>

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

    int events_processed = 0;
    uint64_t now = aosl_time_ms();

    /* Lock the context for thread safety */
    aosl_lock_lock(ctx->mutex);

    /* Process signaling messages */
    if (ctx->signaling) {
        int ret = tinyrtc_signaling_process(ctx->signaling);
        if (ret > 0) {
            events_processed += ret;
        }
    }

    /* Prepare fds for select */
    fd_set read_fds;
    FD_ZERO(&read_fds);
    int max_fd = -1;

    /* Add all ICE sockets from all peers to fd_set */
    for (int i = 0; i < ctx->num_peers; i++) {
        tinyrtc_peer_connection_t *pc = ctx->peers[i];
        if (pc && pc->ice && pc->ice->socket >= 0) {
            FD_SET(pc->ice->socket, &read_fds);
            if (pc->ice->socket > max_fd) {
                max_fd = pc->ice->socket;
            }
        }
    }

    aosl_lock_unlock(ctx->mutex);

    /* Do select with timeout */
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

    /* Now process each ready socket */
    aosl_lock_lock(ctx->mutex);

    uint8_t buffer[2048]; /* 2KB MTU is enough for UDP */
    for (int i = 0; i < ctx->num_peers; i++) {
        tinyrtc_peer_connection_t *pc = ctx->peers[i];
        if (!pc || !pc->ice || pc->ice->socket < 0) {
            continue;
        }

        /* Check for incoming UDP packets first */
        if (FD_ISSET(pc->ice->socket, &read_fds)) {
            /* Read packet */
            int len = recv(pc->ice->socket, buffer, sizeof(buffer), 0);
            if (len > 0) {
                TINYRTC_LOG_INFO("Received UDP packet: %d bytes, first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                    len,
                    buffer[0], buffer[1], buffer[2], buffer[3],
                    buffer[4], buffer[5], buffer[6], buffer[7]);

                /* Check if this is a STUN packet or media/DTLS packet */
                int is_stun = ice_process_packet(pc->ice, buffer, len);
                TINYRTC_LOG_INFO("  -> STUN check result: %s", is_stun ? "NOT STUN (media/DTLS)" : "STUN packet");

                if (!is_stun) {
                    if (pc->dtls != NULL && !pc->srtp_initialized) {
                        /* This is DTLS handshake data - process it */
                        int ret = dtls_process_data(pc->dtls, buffer, len);
                        TINYRTC_LOG_DEBUG("Processed DTLS packet: %d", ret);
                        events_processed++;
                    } else {
                        /* This is RTP media, process it */
                        pc_process_incoming_rtp(pc, buffer, len);
                        events_processed++;
                    }
                }
                events_processed++;
            } else if (len < 0) {
                TINYRTC_LOG_ERROR("recv() failed: %s", strerror(errno));
            }
        } else {
            TINYRTC_LOG_DEBUG("No data ready on ICE socket fd=%d", pc->ice->socket);
        }
    }

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
