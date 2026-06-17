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

static tinyrtc_error_t pc_initialize_srtp_after_dtls(tinyrtc_peer_connection_t *pc)
{
    unsigned char client_key[16];
    unsigned char server_key[16];
    unsigned char client_salt[14];
    unsigned char server_salt[14];
    srtp_context_t *send_ctx = NULL;
    srtp_context_t *recv_ctx = NULL;
    tinyrtc_error_t err;

    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(pc->dtls != NULL, TINYRTC_ERROR_INVALID_STATE);

    TINYRTC_LOG_DEBUG("Deriving SRTP keys");
    err = dtls_derive_srtp_keys(pc->dtls, client_key, client_salt, server_key, server_salt);
    if (err != TINYRTC_OK) {
        TINYRTC_LOG_ERROR("Failed to derive SRTP keys: %s", tinyrtc_get_error_string(err));
        return err;
    }

    if (pc->config.is_initiator) {
        send_ctx = srtp_init(client_key, client_salt);
        recv_ctx = srtp_init(server_key, server_salt);
    } else {
        send_ctx = srtp_init(server_key, server_salt);
        recv_ctx = srtp_init(client_key, client_salt);
    }

    if (send_ctx == NULL || recv_ctx == NULL) {
        if (send_ctx != NULL) {
            srtp_destroy(send_ctx);
        }
        if (recv_ctx != NULL) {
            srtp_destroy(recv_ctx);
        }
        TINYRTC_LOG_ERROR("Failed to initialize SRTP");
        return TINYRTC_ERROR_MEMORY;
    }

    pc->srtp = send_ctx;
    pc->srtp_rx = recv_ctx;
    pc->srtp_initialized = true;
    TINYRTC_LOG_INFO("SRTP initialized successfully, media encryption ready");
    fflush(stdout);
    return TINYRTC_OK;
}

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
            struct sockaddr_in source_addr;
            socklen_t source_len = sizeof(source_addr);
            int len;

            memset(&source_addr, 0, sizeof(source_addr));
            len = recvfrom(pc->ice->socket, buffer, sizeof(buffer), 0,
                           (struct sockaddr *)&source_addr, &source_len);
            if (len > 0) {
                TINYRTC_LOG_DEBUG("Received UDP packet: %d bytes, first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                    len,
                    buffer[0], buffer[1], buffer[2], buffer[3],
                    buffer[4], buffer[5], buffer[6], buffer[7]);

                /* ice_process_packet returns: 0 = STUN packet (handled), 1 = not STUN (media/DTLS) */
                int is_media = ice_process_packet(pc->ice, buffer, len, &source_addr);
                TINYRTC_LOG_DEBUG("  -> packet classification: %s", is_media ? "media/DTLS" : "STUN (handled by ICE)");

                if (is_media) {
                    /* This is media/DTLS packet (not STUN) */
                    if (pc->dtls != NULL && !pc->srtp_initialized && ice_is_connected(pc->ice)) {
                        /* This is DTLS handshake data - process it */
                        int ret = dtls_process_data(pc->dtls, buffer, len);
                        TINYRTC_LOG_DEBUG("Processed DTLS packet: %d", ret);
                        events_processed++;
                    } else {
                        /* This is RTP media, process it */
                        (void)pc_process_incoming_rtp(pc, buffer, len);
                        events_processed++;
                    }
                }
                events_processed++;
            } else if (len < 0) {
                TINYRTC_LOG_ERROR("recv() failed: %s", strerror(errno));
            }
        }

        /* Run ICE connectivity checks (send pings, handle timeouts) */
        ice_check_connectivity(pc->ice, now);

        /* Check if ICE connected and notify state change */
        bool was_connected = pc->state == TINYRTC_PC_STATE_CONNECTED;
        bool is_connected = ice_is_connected(pc->ice);
        if (!was_connected && is_connected) {
            pc->state = TINYRTC_PC_STATE_CONNECTED;
            /* Start DTLS handshake now that ICE is connected */
            if (pc->dtls == NULL) {
                dtls_role_t role = pc->config.is_initiator ? DTLS_ROLE_CLIENT : DTLS_ROLE_SERVER;
                pc->dtls = dtls_init(role);
                if (pc->dtls == NULL) {
                    TINYRTC_LOG_ERROR("Failed to initialize DTLS");
                }
            }
            if (pc->dtls != NULL) {
                dtls_role_t role = pc->config.is_initiator ? DTLS_ROLE_CLIENT : DTLS_ROLE_SERVER;
                dtls_start(pc->dtls, pc->ice);
                TINYRTC_LOG_INFO("DTLS handshake started (role=%s)",
                    role == DTLS_ROLE_CLIENT ? "client" : "server");
            }
            if (pc->config.observer.on_connection_state_change) {
                pc->config.observer.on_connection_state_change(
                    pc->config.observer.user_data,
                    pc->state);
            }
            TINYRTC_LOG_INFO("ICE connected successfully, starting DTLS handshake (initiator=%d)", pc->config.is_initiator);
            fflush(stdout);
        }

        /* Process DTLS if DTLS is started */
        if (pc->dtls != NULL && !pc->srtp_initialized && ice_is_connected(pc->ice)) {
            dtls_poll_handshake(pc->dtls);
            int dtls_done = dtls_is_handshake_complete(pc->dtls);
            TINYRTC_LOG_DEBUG("Peer %p: DTLS handshake check done, complete=%d", pc, dtls_done);
            if (dtls_done) {
                (void)pc_initialize_srtp_after_dtls(pc);
            }
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
