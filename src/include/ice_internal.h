/**
 * @file ice_internal.h
 * @brief ICE (Interactive Connectivity Establishment) internal structures and functions
 *
 * ICE handles NAT traversal, candidate gathering, and connectivity checks.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_ICE_INTERNAL_H
#define TINYRTC_ICE_INTERNAL_H

#include "common.h"
#include "sdp_internal.h"
#include "media.h"
#include "tinyrtc/peer_connection.h"

/* STUN constants and types */
#define STUN_MAGIC_COOKIE 0x2112A442

/* STUN message types */
#define STUN_BINDING_REQUEST      0x0001
#define STUN_BINDING_RESPONSE     0x0101
#define STUN_BINDING_ERROR_RESPONSE 0x0111

/* STUN attributes */
#define STUN_ATTR_MAPPED_ADDRESS    0x0001
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020
#define STUN_ATTR_ERROR_CODE        0x0004
#define STUN_ATTR_MESSAGE_INTEGRITY 0x0008
#define STUN_ATTR_FINGERPRINT      0x8028
#define STUN_ATTR_SOURCE_ADDRESS    0x0004

typedef struct {
    uint16_t type;
    uint16_t length;
    uint32_t magic_cookie;
    uint8_t transaction_id[12];
} stun_header_t;

/* =============================================================================
 * ICE Constants
 * ========================================================================== */

#define ICE_MAX_CANDIDATES      TINYRTC_MAX_CANDIDATES
#define ICE_STUN_TIMEOUT_MS      1000
#define ICE_MAX_RETRIES          3

/* Candidate types */
typedef enum {
    ICE_CANDIDATE_TYPE_HOST = 0,
    ICE_CANDIDATE_TYPE_SRFLX = 1,
    ICE_CANDIDATE_TYPE_RELAY = 2,
} ice_candidate_type_t;

/* Candidate priority */
#define ICE_PRIORITY_HOST       (126 << 24) | (0 << 8) | 126
#define ICE_PRIORITY_SRFLX      (110 << 24) | (0 << 8) | 126
#define ICE_PRIORITY_RELAY       (100 << 24) | (0 << 8) | 126

/* =============================================================================
 * ICE Candidate internal structure
 * ========================================================================== */

typedef struct {
    ice_candidate_type_t type;
    uint32_t priority;
    char ip[64];
    uint16_t port;
    char protocol[16];   /* udp/tcp */
    bool is_ipv6;
    bool selected;         /* Is this the selected candidate */
    /* For STUN binding request */
    uint64_t transaction_id;
    int retries;
    uint64_t last_sent_ms;
} ice_candidate_internal_t;

/* =============================================================================
 * ICE check pair
 * ========================================================================== */

typedef struct {
    ice_candidate_internal_t *local;
    ice_candidate_internal_t *remote;
    bool succeeded;
    uint64_t last_ping_ms;
    int rtt;
} ice_check_pair_t;

/* =============================================================================
 * ICE session structure
 * ========================================================================== */

typedef struct {
    tinyrtc_peer_connection_t *pc;
    int state;                  /* 0 = new, 1 = gathering, 2 = gathered, 3 = checking, 4 = done */
    int num_local_candidates;
    int num_remote_candidates;
    ice_candidate_internal_t local[ICE_MAX_CANDIDATES];
    ice_candidate_internal_t remote[ICE_MAX_CANDIDATES];
    int num_check_pairs;
    ice_check_pair_t check_pairs[ICE_MAX_CANDIDATES * ICE_MAX_CANDIDATES];
    ice_check_pair_t *selected_pair;
    int socket;       /* UDP socket for ICE connectivity checks (int fd) */
    bool gathering_complete;
} ice_session_t;

/* =============================================================================
 * STUN functions
 * ========================================================================== */

/**
 * @brief Send STUN binding request to get server reflexive candidate
 *
 * @param ice ICE session
 * @param server_addr Server address
 * @param port Server port
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t stun_send_binding_request(ice_session_t *ice,
                                          const char *server_addr,
                                          uint16_t port);

/**
 * @brief Process STUN response
 *
 * @param ice ICE session
 * @param data Response data
 * @param len Response length
 * @param[out] mapped_addr Output mapped address
 * @param mapped_addr_len Output buffer size
 * @param[out] mapped_port Output mapped port
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t stun_process_response(ice_session_t *ice,
                                       const uint8_t *data, size_t len,
                                       char *mapped_addr, size_t mapped_addr_len,
                                       uint16_t *mapped_port);

/**
 * @brief Create STUN binding response to response to connectivity check
 *
 * @param buffer Output buffer
 * @param buf_size Buffer size
 * @param request_header Request header from incoming request
 * @return Size of created response
 */
size_t stun_create_binding_response(uint8_t *buffer, size_t buf_size,
                                     stun_header_t *request_header);

/* =============================================================================
 * ICE functions
 * ========================================================================== */

/**
 * @brief Initialize ICE session for peer connection
 *
 * @param pc Parent peer connection
 * @return New ICE session, NULL on error
 */
ice_session_t *ice_session_create(tinyrtc_peer_connection_t *pc);

/**
 * @brief Destroy ICE session
 *
 * @param ice ICE session to destroy
 */
void ice_session_destroy(ice_session_t *ice);

/**
 * @brief Start gathering local candidates
 *
 * Gathers host candidates (local interface addresses) and then
 * sends STUN requests to get server reflexive candidates.
 *
 * @param ice ICE session
 * @param stun_server STUN server address
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t ice_start_gathering(ice_session_t *ice, const char *stun_server);

/**
 * @brief Add remote candidate from SDP
 *
 * @param ice ICE session
 * @param candidate SDP candidate
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t ice_add_remote_candidate(ice_session_t *ice,
                                           const sdp_candidate_t *candidate);

/**
 * @brief Process incoming packet (STUN or media)
 *
 * @param ice ICE session
 * @param data Packet data
 * @param len Packet length
 * @return 0 if handled as STUN, 1 if it's media packet
 */
int ice_process_packet(ice_session_t *ice, const uint8_t *data, size_t len);

/**
 * @brief Check for ICE connectivity
 *
 * Called periodically from process_events to send pings and handle timeouts.
 *
 * @param ice ICE session
 * @param now Current time in ms
 */
void ice_check_connectivity(ice_session_t *ice, uint64_t now);

/**
 * @brief Check if ICE is connected
 *
 * @param ice ICE session
 * @return true if we have a selected candidate pair that succeeded
 */
bool ice_is_connected(ice_session_t *ice);

/**
 * @brief Convert public candidate to SDP structure
 *
 * @param src Internal ICE candidate
 * @param dst Output SDP candidate
 */
void ice_candidate_to_sdp(const ice_candidate_internal_t *src,
                           sdp_candidate_t *dst);

/**
 * @brief Convert SDP candidate to internal ICE candidate
 *
 * @param src Input SDP candidate
 * @param dst Output internal ICE candidate
 */
void ice_candidate_from_sdp(const sdp_candidate_t *src,
                             ice_candidate_internal_t *dst);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_ICE_INTERNAL_H */
