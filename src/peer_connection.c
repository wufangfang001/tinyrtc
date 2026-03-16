/**
 * @file peer_connection.c
 * @brief TinyRTC PeerConnection implementation
 *
 * This is the main public API for creating and managing WebRTC connections.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include <stdio.h>
#include "common.h"
#include "sdp_internal.h"
#include "peer_connection_internal.h"
#include "tinyrtc/peer_connection.h"

/* tinyrtc.h forward declares struct tinyrtc_context,
 * but the full definition isn't visible to us because it's in src/tinyrtc.c.
 * We just need to declare it here since common.h is already included, and the
 * definition is compatible because it's the same translation unit layout.
 */
struct tinyrtc_context {
    tinyrtc_config_t config;
    tinyrtc_log_level_t log_level;
    aosl_lock_t mutex;  /* For thread safety when accessing global state */
    int num_peers;        /* Number of active peer connections */
};

/* =============================================================================
 * Public API implementation - Creation/Destruction
 * ========================================================================== */

tinyrtc_peer_connection_t *tinyrtc_peer_connection_create(
    tinyrtc_context_t *ctx,
    const tinyrtc_pc_config_t *config)
{
    TINYRTC_CHECK_NULL(ctx);
    TINYRTC_CHECK_NULL(config);

    tinyrtc_peer_connection_t *pc = (tinyrtc_peer_connection_t *)tinyrtc_calloc(1, sizeof(*pc));
    if (pc == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_create: memory allocation failed");
        return NULL;
    }

    pc->ctx = ctx;
    pc->config = *config;
    pc->state = TINYRTC_PC_STATE_NEW;
    pc->num_local_tracks = 0;
    pc->num_remote_tracks = 0;
    pc->ice_gathering_state = 0;
    pc->dtls_state = 0;
    pc->srtp_initialized = false;

    sdp_session_init(&pc->local_sdp);
    sdp_session_init(&pc->remote_sdp);

    /* Initialize mutex */
    pc->mutex = aosl_lock_create();
    if (pc->mutex == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_create: lock creation failed");
        tinyrtc_internal_free(pc);
        return NULL;
    }

    /* Increment peer count */
    aosl_lock_lock(ctx->mutex);
    ctx->num_peers++;
    aosl_lock_unlock(ctx->mutex);

    TINYRTC_LOG_DEBUG("PeerConnection created, initiator=%d", config->is_initiator);

    return pc;
}

void tinyrtc_peer_connection_destroy(tinyrtc_peer_connection_t *pc)
{
    if (pc == NULL) {
        return;
    }

    /* Close first if not already closed */
    if (pc->state != TINYRTC_PC_STATE_CLOSED) {
        tinyrtc_peer_connection_close(pc);
    }

    /* Free local tracks */
    for (int i = 0; i < pc->num_local_tracks; i++) {
        tinyrtc_internal_free(pc->local_tracks[i]);
    }

    /* Free remote tracks */
    for (int i = 0; i < pc->num_remote_tracks; i++) {
        tinyrtc_internal_free(pc->remote_tracks[i]);
    }

    /* Destroy mutex */
    aosl_lock_destroy(pc->mutex);

    /* Decrement peer count in context */
    aosl_lock_lock(pc->ctx->mutex);
    pc->ctx->num_peers--;
    aosl_lock_unlock(pc->ctx->mutex);

    tinyrtc_internal_free(pc);
    TINYRTC_LOG_DEBUG("PeerConnection destroyed");
}

void tinyrtc_peer_connection_close(tinyrtc_peer_connection_t *pc)
{
    if (pc == NULL) {
        return;
    }

    aosl_lock_lock(pc->mutex);
    pc->state = TINYRTC_PC_STATE_CLOSED;
    aosl_lock_unlock(pc->mutex);

    /* TODO: Close all sockets, cancel all timers */

    if (pc->config.observer.on_connection_state_change != NULL) {
        pc->config.observer.on_connection_state_change(
            pc->config.observer.user_data,
            pc->state);
    }

    TINYRTC_LOG_DEBUG("PeerConnection closed");
}

tinyrtc_pc_state_t tinyrtc_peer_connection_get_state(tinyrtc_peer_connection_t *pc)
{
    if (pc == NULL) {
        return TINYRTC_PC_STATE_FAILED;
    }
    return pc->state;
}

/* =============================================================================
 * Public API implementation - Tracks
 * ========================================================================== */

tinyrtc_track_t *tinyrtc_peer_connection_add_track(
    tinyrtc_peer_connection_t *pc,
    const tinyrtc_track_config_t *config)
{
    TINYRTC_CHECK_NULL(pc);
    TINYRTC_CHECK_NULL(config);

    if (pc->num_local_tracks >= SDP_MAX_MEDIA) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_add_track: too many tracks (max %d)",
            SDP_MAX_MEDIA);
        return NULL;
    }

    tinyrtc_track_t *track = (tinyrtc_track_t *)tinyrtc_calloc(1, sizeof(*track));
    if (track == NULL) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_add_track: memory allocation failed");
        return NULL;
    }

    track->kind = config->kind;
    track->codec_id = config->codec_id;
    if (config->payload_type != 0) {
        track->payload_type = config->payload_type;
    } else {
        track->payload_type = tinyrtc_codec_get_default_payload(config->codec_id);
    }
    if (config->clock_rate != 0) {
        track->clock_rate = config->clock_rate;
    } else {
        track->clock_rate = tinyrtc_codec_get_clock_rate(config->codec_id);
    }
    strncpy(track->mid, config->mid, sizeof(track->mid) - 1);
    track->is_local = true;
    track->pc = pc;
    track->frames_sent = 0;
    track->bytes_sent = 0;

    aosl_lock_lock(pc->mutex);
    pc->local_tracks[pc->num_local_tracks++] = track;
    aosl_lock_unlock(pc->mutex);

    TINYRTC_LOG_DEBUG("Added local track: mid=%s, codec=%s",
        track->mid, tinyrtc_codec_get_name(track->codec_id));

    return track;
}

tinyrtc_error_t tinyrtc_peer_connection_remove_track(
    tinyrtc_peer_connection_t *pc,
    tinyrtc_track_t *track)
{
    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(track != NULL, TINYRTC_ERROR_INVALID_ARG);

    /* Find and remove from list */
    aosl_lock_lock(pc->mutex);
    bool found = false;
    for (int i = 0; i < pc->num_local_tracks; i++) {
        if (pc->local_tracks[i] == track) {
            /* Shift remaining tracks */
            for (int j = i; j < pc->num_local_tracks - 1; j++) {
                pc->local_tracks[j] = pc->local_tracks[j + 1];
            }
            pc->num_local_tracks--;
            found = true;
            break;
        }
    }
    aosl_lock_unlock(pc->mutex);

    if (!found) {
        return TINYRTC_ERROR_NOT_FOUND;
    }

    tinyrtc_internal_free(track);
    return TINYRTC_OK;
}

/* =============================================================================
 * Public API implementation - Track getters
 * ========================================================================== */

tinyrtc_track_kind_t tinyrtc_track_get_kind(tinyrtc_track_t *track)
{
    if (track == NULL) {
        return TINYRTC_TRACK_KIND_AUDIO;
    }
    return track->kind;
}

tinyrtc_codec_id_t tinyrtc_track_get_codec(tinyrtc_track_t *track)
{
    if (track == NULL) {
        return 0;
    }
    return track->codec_id;
}

const char *tinyrtc_track_get_mid(tinyrtc_track_t *track)
{
    if (track == NULL) {
        return "";
    }
    return track->mid;
}

/* =============================================================================
 * Public API implementation - SDP offer/answer
 * ========================================================================== */

tinyrtc_error_t tinyrtc_peer_connection_create_offer(
    tinyrtc_peer_connection_t *pc,
    char **out_sdp)
{
    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(out_sdp != NULL, TINYRTC_ERROR_INVALID_ARG);

    *out_sdp = NULL;

    aosl_lock_lock(pc->mutex);

    if (pc->state == TINYRTC_PC_STATE_CLOSED) {
        aosl_lock_unlock(pc->mutex);
        return TINYRTC_ERROR_INVALID_STATE;
    }

    pc->state = TINYRTC_PC_STATE_CONNECTING;

    /* Generate SDP offer from local tracks */
    sdp_session_t offer;
    sdp_session_init(&offer);

    /* Set default SDP values */
    offer.version = 0;
    strcpy(offer.username, "-");
    offer.session_id = (uint64_t)aosl_time_ms();
    offer.session_version = (uint64_t)aosl_time_ms();
    strcpy(offer.network_type, "IN");
    strcpy(offer.address_type, "IP4");
    strcpy(offer.unicast_address, "0.0.0.0");
    strcpy(offer.session_name, "TinyRTC");
    offer.start_time = 0;
    offer.stop_time = 0;

    /* Generate random ICE credentials when available
     * TODO: use proper random when we get aosl_random */
    char ufrag[32];
    char pwd[64];
    uint64_t now = (uint64_t)aosl_time_ms();
    snprintf(ufrag, sizeof(ufrag), "tiny%" PRIu64, now);
    snprintf(pwd, sizeof(pwd), "rtc%" PRIu64 "random", now + 12345);
    strcpy(offer.ice_ufrag, ufrag);
    strcpy(offer.ice_pwd, pwd);

    /* DTLS setup: actpass by default */
    strcpy(offer.dtls_setup, "actpass");

    /* Add all local tracks to the offer */
    for (int i = 0; i < pc->num_local_tracks; i++) {
        tinyrtc_track_t *track = pc->local_tracks[i];
        tinyrtc_track_config_t cfg = {0};
        cfg.kind = track->kind;
        cfg.codec_id = track->codec_id;
        cfg.payload_type = track->payload_type;
        cfg.clock_rate = track->clock_rate;
        cfg.mid = track->mid;
        sdp_add_media(&offer, &cfg);
    }

    /* Copy to pc->local_sdp */
    pc->local_sdp = offer;

    /* Generate SDP text */
    tinyrtc_error_t err = sdp_generate(&pc->local_sdp, out_sdp);

    aosl_lock_unlock(pc->mutex);

    if (err != TINYRTC_OK) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_create_offer: SDP generation failed");
        return err;
    }

    TINYRTC_LOG_DEBUG("Created offer, size=%zu bytes", strlen(*out_sdp));

    /* TODO: Start ICE candidate gathering */

    return TINYRTC_OK;
}

tinyrtc_error_t tinyrtc_peer_connection_set_remote_description(
    tinyrtc_peer_connection_t *pc,
    const char *sdp)
{
    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(sdp != NULL, TINYRTC_ERROR_INVALID_ARG);

    aosl_lock_lock(pc->mutex);

    if (pc->state == TINYRTC_PC_STATE_CLOSED) {
        aosl_lock_unlock(pc->mutex);
        return TINYRTC_ERROR_INVALID_STATE;
    }

    /* Parse remote SDP */
    tinyrtc_error_t err = sdp_parse(sdp, &pc->remote_sdp);
    if (err != TINYRTC_OK) {
        aosl_lock_unlock(pc->mutex);
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_set_remote_description: SDP parse failed");
        return err;
    }

    TINYRTC_LOG_DEBUG("Remote description set: %d media tracks", pc->remote_sdp.num_media);

    /* Process remote media - add remote tracks */
    for (int i = 0; i < pc->remote_sdp.num_media; i++) {
        sdp_media_t *media = &pc->remote_sdp.media[i];

        tinyrtc_track_t *track = (tinyrtc_track_t *)tinyrtc_calloc(1, sizeof(*track));
        if (track == NULL) {
            continue;
        }
        track->kind = media->kind;
        track->codec_id = media->codec_id;
        track->payload_type = media->payload_type;
        track->clock_rate = media->clock_rate;
        strcpy(track->mid, media->mid);
        track->is_local = false;
        track->pc = pc;

        if (pc->num_remote_tracks < SDP_MAX_MEDIA) {
            pc->remote_tracks[pc->num_remote_tracks++] = track;

            /* Notify application */
            if (pc->config.observer.on_track_added != NULL) {
                pc->config.observer.on_track_added(
                    pc->config.observer.user_data,
                    track);
            }
        } else {
            tinyrtc_internal_free(track);
        }
    }

    aosl_lock_unlock(pc->mutex);

    return TINYRTC_OK;
}

tinyrtc_error_t tinyrtc_peer_connection_create_answer(
    tinyrtc_peer_connection_t *pc,
    char **out_sdp)
{
    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(out_sdp != NULL, TINYRTC_ERROR_INVALID_ARG);

    *out_sdp = NULL;

    aosl_lock_lock(pc->mutex);

    if (pc->state == TINYRTC_PC_STATE_CLOSED) {
        aosl_lock_unlock(pc->mutex);
        return TINYRTC_ERROR_INVALID_STATE;
    }

    pc->state = TINYRTC_PC_STATE_CONNECTING;

    /* Create answer based on remote offer */
    sdp_session_t answer;
    sdp_session_init(&answer);

    /* Copy basic fields from offer, increment version */
    answer.version = 0;
    strcpy(answer.username, pc->remote_sdp.username);
    answer.session_id = pc->remote_sdp.session_id;
    answer.session_version = pc->remote_sdp.session_version + 1;
    strcpy(answer.network_type, pc->remote_sdp.network_type);
    strcpy(answer.address_type, pc->remote_sdp.address_type);
    strcpy(answer.unicast_address, "0.0.0.0");
    strcpy(answer.session_name, "TinyRTC");
    answer.start_time = 0;
    answer.stop_time = 0;

    /* Copy ICE credentials from local */
    strcpy(answer.ice_ufrag, pc->local_sdp.ice_ufrag);
    strcpy(answer.ice_pwd, pc->local_sdp.ice_pwd);

    /* DTLS fingerprint copied from local, setup is active */
    if (pc->local_sdp.fingerprint[0] != '\0') {
        strcpy(answer.fingerprint_type, pc->local_sdp.fingerprint_type);
        strcpy(answer.fingerprint, pc->local_sdp.fingerprint);
    }
    strcpy(answer.dtls_setup, "active");

    /* Match local tracks to remote offer and answer */
    for (int i = 0; i < pc->remote_sdp.num_media; i++) {
        sdp_media_t *remote_media = &pc->remote_sdp.media[i];

        /* Find matching local track by kind
         * In simple case with one track per kind, this works
         */
        tinyrtc_track_t *matched = NULL;
        for (int j = 0; j < pc->num_local_tracks; j++) {
            if (pc->local_tracks[j]->kind == remote_media->kind) {
                matched = pc->local_tracks[j];
                break;
            }
        }

        if (matched != NULL) {
            tinyrtc_track_config_t cfg = {0};
            cfg.kind = matched->kind;
            cfg.codec_id = matched->codec_id;
            cfg.payload_type = matched->payload_type;
            cfg.clock_rate = matched->clock_rate;
            cfg.mid = matched->mid;
            sdp_add_media(&answer, &cfg);
        }
        /* If no match, still create media entry but it will be inactive */
    }

    /* Store answer locally */
    pc->local_sdp = answer;

    /* Generate SDP text */
    tinyrtc_error_t err = sdp_generate(&pc->local_sdp, out_sdp);

    aosl_lock_unlock(pc->mutex);

    if (err != TINYRTC_OK) {
        TINYRTC_LOG_ERROR("tinyrtc_peer_connection_create_answer: SDP generation failed");
        return err;
    }

    TINYRTC_LOG_DEBUG("Created answer, size=%zu bytes", strlen(*out_sdp));

    /* TODO: Start ICE candidate gathering */

    return TINYRTC_OK;
}

tinyrtc_error_t tinyrtc_peer_connection_add_ice_candidate(
    tinyrtc_peer_connection_t *pc,
    tinyrtc_ice_candidate_t *candidate)
{
    TINYRTC_CHECK(pc != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(candidate != NULL, TINYRTC_ERROR_INVALID_ARG);

    aosl_lock_lock(pc->mutex);

    int added = sdp_add_candidate(&pc->remote_sdp, candidate);

    aosl_lock_unlock(pc);

    if (added < 0) {
        TINYRTC_LOG_WARN("tinyrtc_peer_connection_add_ice_candidate: too many candidates");
        return TINYRTC_ERROR;
    }

    TINYRTC_LOG_DEBUG("Added remote ICE candidate: %s:%d", candidate->ip, candidate->port);

    /* TODO: Add to ICE candidate list and start connectivity checks */

    return TINYRTC_OK;
}

/* =============================================================================
 * Public API implementation - Media send
 * ========================================================================== */

tinyrtc_error_t tinyrtc_track_send_audio_frame(
    tinyrtc_track_t *track,
    const uint8_t *frame,
    size_t frame_len,
    uint32_t timestamp)
{
    TINYRTC_CHECK(track != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(frame != NULL || frame_len == 0, TINYRTC_ERROR_INVALID_ARG);

    if (!track->is_local) {
        return TINYRTC_ERROR_INVALID_STATE;
    }

    /* TODO:
     * 1. Packetize frame into RTP packets
     * 2. Send RTP packets via ICE connected transport
     * 3. Update statistics
     */

    track->frames_sent++;
    track->bytes_sent += frame_len;

    return TINYRTC_OK;
}

tinyrtc_error_t tinyrtc_track_send_video_frame(
    tinyrtc_track_t *track,
    const uint8_t *frame,
    size_t frame_len,
    uint32_t timestamp)
{
    TINYRTC_CHECK(track != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(frame != NULL || frame_len == 0, TINYRTC_ERROR_INVALID_ARG);

    if (!track->is_local) {
        return TINYRTC_ERROR_INVALID_STATE;
    }

    /* TODO:
     * 1. Packetize frame into RTP packets
     * 2. Send RTP packets via ICE connected transport
     * 3. Update statistics
     */

    track->frames_sent++;
    track->bytes_sent += frame_len;

    return TINYRTC_OK;
}
