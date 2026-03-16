/**
 * @file peer_connection_internal.h
 * @brief PeerConnection internal header - private structures and functions
 *
 * This contains the private implementation details for PeerConnection.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_PEER_CONNECTION_INTERNAL_H
#define TINYRTC_PEER_CONNECTION_INTERNAL_H

#include "common.h"
#include "sdp_internal.h"
#include "tinyrtc/peer_connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Track internal structure
 * ========================================================================== */

/**
 * @brief Internal media track structure
 */
struct tinyrtc_track {
    tinyrtc_track_kind_t kind;
    tinyrtc_codec_id_t codec_id;
    char mid[16];
    int payload_type;
    uint32_t clock_rate;
    bool is_local;            /* true = local track, false = remote */
    tinyrtc_peer_connection_t *pc; /* Back reference to parent */

    /* Statistics */
    uint64_t frames_sent;
    uint64_t bytes_sent;
};

/* =============================================================================
 * PeerConnection internal structure
 * ========================================================================== */

/**
 * @brief Internal PeerConnection structure
 */
struct tinyrtc_peer_connection {
    tinyrtc_context_t *ctx;           /* Parent context */
    tinyrtc_pc_config_t config;       /* Configuration */
    tinyrtc_pc_state_t state;         /* Current connection state */

    /* Local tracks */
    int num_local_tracks;
    tinyrtc_track_t *local_tracks[SDP_MAX_MEDIA];

    /* Remote tracks */
    int num_remote_tracks;
    tinyrtc_track_t *remote_tracks[SDP_MAX_MEDIA];

    /* Parsed SDP */
    sdp_session_t local_sdp;
    sdp_session_t remote_sdp;

    /* ICE gathering state */
    int ice_gathering_state;          /* 0 = new, 1 = gathering, 2 = complete */
    int num_local_candidates;

    /* DTLS state */
    int dtls_state;                    /* 0 = new, 1 = connecting, 2 = connected */

    /* SRTP keys (filled after DTLS completes) */
    bool srtp_initialized;
    // TODO: srtp keys when we implement DTLS/SRTP

    /* Mutex for state protection */
    aosl_lock_t mutex;

    /* Back reference to linked list in context (TODO) */
    // struct tinyrtc_peer_connection *next;
};

/* =============================================================================
 * Internal functions
 * ========================================================================== */

/**
 * @brief Negotiate codec based on remote SDP
 *
 * Match local codec preferences with remote capabilities.
 *
 * @param pc Peer connection
 * @param media Remote media description
 * @param[out] payload_type Out matched payload type
 * @return true if codec matched
 */
bool pc_negotiate_codec(tinyrtc_peer_connection_t *pc,
                       const sdp_media_t *remote_media,
                       int *payload_type);

/**
 * @brief Generate SDP offer based on local tracks
 *
 * @param pc Peer connection
 * @param session Output SDP session
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t pc_generate_local_offer(tinyrtc_peer_connection_t *pc,
                                         sdp_session_t *session);

/**
 * @brief Process remote offer and create answer
 *
 * @param pc Peer connection
 * @param remote Remote SDP session
 * @param answer Output answer SDP
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t pc_process_remote_offer(tinyrtc_peer_connection_t *pc,
                                          const sdp_session_t *remote,
                                          sdp_session_t *answer);

/**
 * @brief Set remote description after offer/answer exchange
 *
 * @param pc Peer connection
 * @param remote Parsed remote SDP
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t pc_set_remote_description(tinyrtc_peer_connection_t *pc,
                                            const sdp_session_t *remote);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_PEER_CONNECTION_INTERNAL_H */
