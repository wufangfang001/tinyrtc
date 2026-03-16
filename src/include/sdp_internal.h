/**
 * @file sdp_internal.h
 * @brief SDP (Session Description Protocol) internal structures and utilities
 *
 * This module handles parsing and generating SDP descriptions for WebRTC.
 * SDP is used for exchanging session information between peers during offer/answer.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_SDP_INTERNAL_H
#define TINYRTC_SDP_INTERNAL_H

#include "common.h"
#include "tinyrtc/peer_connection.h"
#include "tinyrtc/media_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * SDP Constants
 * ========================================================================== */

#define SDP_MAX_LINE_LEN      1024
#define SDP_MAX_SESSIONS      1
#define SDP_MAX_MEDIA          2  /* Typically one audio, one video */
#define SDP_MAX_BANDWIDTH      100000
#define SDP_MAX_CANDIDATES     TINYRTC_MAX_CANDIDATES

/* =============================================================================
 * SDP Media track information
 * ========================================================================== */

/**
 * @brief SDP media description - represents one media track
 */
typedef struct {
    tinyrtc_track_kind_t kind;           /* Media type (audio/video) */
    int port;                           /* Media port */
    int proto;                          /* Protocol (RTP/SAVPF) */
    int payload_type;                   /* RTP payload type */
    tinyrtc_codec_id_t codec_id;        /* Codec ID */
    uint32_t clock_rate;                /* RTP clock rate */
    char mid[16];                       /* Media identification */
    bool direction_send;                /* We send */
    bool direction_recv;                /* We receive */
} sdp_media_t;

/* =============================================================================
 * SDP ICE candidate information
 * ========================================================================== */

/**
 * @brief SDP ICE candidate representation
 */
typedef struct {
    char foundation[32];
    uint32_t priority;
    char ip[64];
    uint16_t port;
    char type[32];         /* host, srflx, relay */
    char protocol[16];     /* udp, tcp */
    bool is_ipv6;
} sdp_candidate_t;

/* =============================================================================
 * SDP full session description
 * ========================================================================== */

/**
 * @brief Complete parsed SDP session description
 */
typedef struct {
    /* Version */
    int version;

    /* Originator */
    char username[256];
    uint64_t session_id;
    uint64_t session_version;
    char network_type[16];
    char address_type[16];
    char unicast_address[64];

    /* Session name */
    char session_name[256];

    /* Timing */
    uint64_t start_time;
    uint64_t stop_time;

    /* ICE parameters */
    char ice_ufrag[64];          /* ICE username fragment */
    char ice_pwd[128];           /* ICE password */
    char fingerprint[128];       /* DTLS fingerprint */
    char fingerprint_type[16];   /* SHA-256 */

    /* Media tracks */
    int num_media;
    sdp_media_t media[SDP_MAX_MEDIA];

    /* ICE candidates (gathered so far) */
    int num_candidates;
    sdp_candidate_t candidates[SDP_MAX_CANDIDATES];

    /* DTLS setup (active/passive/actpass) */
    char dtls_setup[16];
} sdp_session_t;

/* =============================================================================
 * SDP parser/generator functions
 * ========================================================================== */

/**
 * @brief Initialize an empty SDP session structure
 *
 * @param session Session structure to initialize
 */
void sdp_session_init(sdp_session_t *session);

/**
 * @brief Parse SDP text into session structure
 *
 * @param text SDP text to parse
 * @param session Output parsed session
 * @return TINYRTC_OK on success, error code on failure
 */
tinyrtc_error_t sdp_parse(const char *text, sdp_session_t *session);

/**
 * @brief Generate SDP text from session structure
 *
 * @param session Session to generate from
 * @param[out] out_text Output allocated SDP string, must be freed with tinyrtc_free()
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t sdp_generate(const sdp_session_t *session, char **out_text);

/**
 * @brief Add a media track to SDP session
 *
 * @param session SDP session
 * @param track_config Track configuration
 * @return 0 on success
 */
int sdp_add_media(sdp_session_t *session, const tinyrtc_track_config_t *track_config);

/**
 * @brief Add an ICE candidate to SDP session
 *
 * @param session SDP session
 * @param candidate ICE candidate to add
 * @return 0 on success
 */
int sdp_add_candidate(sdp_session_t *session, const tinyrtc_ice_candidate_t *candidate);

/**
 * @brief Find media track by MID
 *
 * @param session SDP session
 * @param mid MID to find
 * @return Pointer to media, NULL if not found
 */
sdp_media_t *sdp_find_media_by_mid(sdp_session_t *session, const char *mid);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_SDP_INTERNAL_H */
