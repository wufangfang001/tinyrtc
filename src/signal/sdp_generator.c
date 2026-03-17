/**
 * @file sdp_generator.c
 * @brief SDP (Session Description Protocol) generator implementation
 *
 * Generates SDP text from structured session description.
 * Follows RFC 4566 and WebRTC conventions.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "sdp_internal.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* =============================================================================
 * Local helper macros
 * ========================================================================== */

/**
 * Append a line to the buffer, increment offset
 */
#define SDP_APPEND_LINE(buf, buf_size, offset, fmt, ...) \
    do { \
        int n = snprintf((buf) + (offset), (buf_size) - (offset), fmt "\r\n", ##__VA_ARGS__); \
        if (n > 0) { \
            (offset) += n; \
        } \
    } while (0)

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

tinyrtc_error_t sdp_generate(const sdp_session_t *session, char **out_text)
{
    TINYRTC_CHECK_NULL_RET(TINYRTC_ERROR_INVALID_ARG, session);
    TINYRTC_CHECK_NULL_RET(TINYRTC_ERROR_INVALID_ARG, out_text);

    /* Estimate maximum size: each line is ~80 chars, ~100 lines total = 8KB */
    const size_t estimated_size = 8 * 1024;
    char *buf = (char *)tinyrtc_malloc(estimated_size);
    if (buf == NULL) {
        TINYRTC_LOG_ERROR("sdp_generate: memory allocation failed");
        return TINYRTC_ERROR_MEMORY;
    }

    size_t offset = 0;
    size_t buf_size = estimated_size;

    /* Mandatory SDP lines */
    SDP_APPEND_LINE(buf, buf_size, offset, "v=%d", session->version);
    SDP_APPEND_LINE(buf, buf_size, offset, "o=%s %llu %llu %s %s %s",
        session->username[0] ? session->username : "-",
        (unsigned long long)session->session_id,
        (unsigned long long)session->session_version,
        session->network_type[0] ? session->network_type : "IN",
        session->address_type[0] ? session->address_type : "IP4",
        session->unicast_address[0] ? session->unicast_address : "0.0.0.0");
    SDP_APPEND_LINE(buf, buf_size, offset, "s=%s",
        session->session_name[0] ? session->session_name : "-");

    /* Timing - WebRTC uses 0 0 for unlimited */
    SDP_APPEND_LINE(buf, buf_size, offset, "t=%llu %llu",
        (unsigned long long)session->start_time,
        (unsigned long long)session->stop_time);

    /* ICE attributes */
    if (session->ice_ufrag[0] != '\0') {
        SDP_APPEND_LINE(buf, buf_size, offset, "a=ice-ufrag:%s", session->ice_ufrag);
    }
    if (session->ice_pwd[0] != '\0') {
        SDP_APPEND_LINE(buf, buf_size, offset, "a=ice-pwd:%s", session->ice_pwd);
    }

    /* DTLS fingerprint */
    if (session->fingerprint[0] != '\0') {
        SDP_APPEND_LINE(buf, buf_size, offset, "a=fingerprint:%s %s",
            session->fingerprint_type, session->fingerprint);
    }

    if (session->dtls_setup[0] != '\0') {
        SDP_APPEND_LINE(buf, buf_size, offset, "a=setup:%s", session->dtls_setup);
    }

    /* Media descriptions */
    for (int i = 0; i < session->num_media; i++) {
        const sdp_media_t *media = &session->media[i];

        const char *media_name = (media->kind == TINYRTC_TRACK_KIND_AUDIO) ? "audio" : "video";
        int payload = media->payload_type;
        const char *codec_name = tinyrtc_codec_get_name(media->codec_id);

        SDP_APPEND_LINE(buf, buf_size, offset, "m=%s %d RTP/SAVPF %d",
            media_name, media->port, payload);

        /* a=rtpmap:payload codec clock-rate/channels */
        SDP_APPEND_LINE(buf, buf_size, offset, "a=rtpmap:%d %s %u/%d",
            payload, codec_name, media->clock_rate, media->channels);

        /* a=mid:... */
        if (media->mid[0] != '\0') {
            SDP_APPEND_LINE(buf, buf_size, offset, "a=mid:%s", media->mid);
        }

        /* Direction attributes */
        if (media->direction_send && media->direction_recv) {
            SDP_APPEND_LINE(buf, buf_size, offset, "a=sendrecv");
        } else if (media->direction_send && !media->direction_recv) {
            SDP_APPEND_LINE(buf, buf_size, offset, "a=sendonly");
        } else if (!media->direction_send && media->direction_recv) {
            SDP_APPEND_LINE(buf, buf_size, offset, "a=recvonly");
        } else {
            SDP_APPEND_LINE(buf, buf_size, offset, "a=inactive");
        }

        /* ICE candidates for this media */
        /* In WebRTC, candidates are at session level when trickle is not used */
        /* We follow the standard placement */
    }

    /* All ICE candidates at session level (per WebRTC convention) */
    for (int i = 0; i < session->num_candidates; i++) {
        const sdp_candidate_t *cand = &session->candidates[i];
        SDP_APPEND_LINE(buf, buf_size, offset,
            "a=candidate:%s 1 %u %s %d %s network %s",
            cand->foundation, cand->priority, cand->ip, cand->port,
            cand->type, cand->protocol);
    }

    /* End of SDP, null terminate */
    buf[offset] = '\0';

    *out_text = buf;
    TINYRTC_LOG_DEBUG("SDP generated: %zu bytes", offset);

    return TINYRTC_OK;
}
