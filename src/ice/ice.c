/**
 * @file ice.c
 * @brief ICE (Interactive Connectivity Establishment) implementation
 *
 * Handles candidate gathering and connectivity checks.
 * Follows RFC 8445.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "ice_internal.h"
#include "tinyrtc/peer_connection.h"

#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* STUN magic cookie */
#define STUN_MAGIC_COOKIE 0x2112A442
#include <unistd.h>

/* =============================================================================
 * Local helpers
 * ========================================================================== */

/**
 * Parse STUN server URL (stun:host:port)
 */
static bool parse_stun_url(const char *url, char *host, size_t host_len, uint16_t *port)
{
    /* Skip "stun:" prefix */
    if (strncmp(url, "stun:", 5) == 0) {
        url += 5;
    }

    const char *colon = strchr(url, ':');
    if (colon == NULL) {
        /* Default port is 3478 */
        strncpy(host, url, host_len - 1);
        *port = 3478;
    } else {
        size_t host_len_actual = colon - url;
        if (host_len_actual >= host_len) {
            host_len_actual = host_len - 1;
        }
        strncpy(host, url, host_len_actual);
        host[host_len_actual] = '\0';
        *port = (uint16_t)atoi(colon + 1);
        if (*port == 0) {
            *port = 3478;
        }
    }

    return host[0] != '\0';
}

/**
 * Calculate candidate priority according to ICE spec
 */
static uint32_t ice_calculate_priority(ice_candidate_type_t type)
{
    /* Priority = (2^24) * typePreference + (2^8) * localPreference + 256 */
    uint32_t type_pref;
    switch (type) {
        case ICE_CANDIDATE_TYPE_HOST:
            type_pref = 126;
            break;
        case ICE_CANDIDATE_TYPE_SRFLX:
            type_pref = 110;
            break;
        case ICE_CANDIDATE_TYPE_RELAY:
            type_pref = 100;
            break;
        default:
            type_pref = 0;
            break;
    }
    return (type_pref << 24) | (0 << 8) | 255;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

ice_session_t *ice_session_create(tinyrtc_peer_connection_t *pc)
{
    TINYRTC_CHECK_NULL(pc);

    ice_session_t *ice = (ice_session_t *)tinyrtc_calloc(1, sizeof(*ice));
    if (ice == NULL) {
        TINYRTC_LOG_ERROR("ice_session_create: memory allocation failed");
        return NULL;
    }

    ice->pc = pc;
    ice->state = 0; /* new */
    ice->num_local_candidates = 0;
    ice->num_remote_candidates = 0;
    ice->num_check_pairs = 0;
    ice->selected_pair = NULL;
    ice->socket = -1; /* AOSL_INVALID_SOCKET = -1 */
    ice->gathering_complete = false;

    TINYRTC_LOG_DEBUG("ICE session created");
    return ice;
}

void ice_session_destroy(ice_session_t *ice)
{
    if (ice == NULL) {
        return;
    }

    if (ice->socket >= 0) {
        aosl_hal_sk_close(ice->socket);
    }

    tinyrtc_internal_free(ice);
    TINYRTC_LOG_DEBUG("ICE session destroyed");
}

tinyrtc_error_t ice_start_gathering(ice_session_t *ice, const char *stun_url)
{
    TINYRTC_CHECK(ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(stun_url != NULL, TINYRTC_ERROR_INVALID_ARG);

    TINYRTC_LOG_DEBUG("Starting ICE candidate gathering from %s", stun_url);

    ice->state = 1; /* gathering */

    /* Parse STUN URL */
    char stun_host[128];
    uint16_t stun_port;
    if (!parse_stun_url(stun_url, stun_host, sizeof(stun_host), &stun_port)) {
        TINYRTC_LOG_ERROR("Invalid STUN URL: %s", stun_url);
        ice->state = 2; /* gathered */
        ice->gathering_complete = true;
        return TINYRTC_ERROR;
    }

    /* TODO: Enumerate local interfaces and gather host candidates */
    /* For now, just add a placeholder host candidate */
    /* In real implementation, we need to enumerate all network interfaces */

    /* Create UDP socket */
    int sock = aosl_hal_sk_socket(AOSL_AF_INET, AOSL_SOCK_DGRAM, AOSL_IPPROTO_UDP);
    if (sock < 0) {
        TINYRTC_LOG_ERROR("Failed to create UDP socket for ICE");
        ice->state = 2;
        ice->gathering_complete = true;
        return TINYRTC_ERROR_NETWORK;
    }

    /* Bind to any port */
    aosl_sockaddr_t addr = {0};
    addr.sa_family = AOSL_AF_INET;
    addr.sa_port = 0; /* Random port */

    int ret = aosl_hal_sk_bind(sock, &addr);
    if (ret < 0) {
        TINYRTC_LOG_ERROR("Failed to bind UDP socket");
        aosl_hal_sk_close(sock);
        ice->state = 2;
        ice->gathering_complete = true;
        return TINYRTC_ERROR_NETWORK;
    }

    ice->socket = sock;

    /* Get actual port we bound to */
    struct sockaddr_in bound_addr;
    socklen_t addr_len = sizeof(bound_addr);
    ret = getsockname((int)sock, (struct sockaddr *)&bound_addr, &addr_len);
    uint16_t local_port = ntohs(bound_addr.sin_port);

    /* Add host candidate for 0.0.0.0 (we just need to send STUN) */
    ice_candidate_internal_t *cand = &ice->local[ice->num_local_candidates];
    memset(cand, 0, sizeof(*cand));
    cand->type = ICE_CANDIDATE_TYPE_HOST;
    strcpy(cand->ip, "0.0.0.0");
    cand->port = local_port;
    strcpy(cand->protocol, "udp");
    cand->is_ipv6 = false;
    cand->priority = ice_calculate_priority(ICE_CANDIDATE_TYPE_HOST);
    cand->selected = false;
    ice->num_local_candidates++;

    /* Send STUN binding request to get server reflexive candidate */
    stun_send_binding_request(ice, stun_host, stun_port);

    ice->state = 2; /* gathered */
    ice->gathering_complete = true;

    TINYRTC_LOG_DEBUG("ICE gathering complete, %d local candidates", ice->num_local_candidates);

    /* Notify application that gathering is complete */
    /* TODO: call observer callback */

    return TINYRTC_OK;
}

tinyrtc_error_t ice_add_remote_candidate(ice_session_t *ice,
                                           const sdp_candidate_t *sdp_cand)
{
    TINYRTC_CHECK(ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(sdp_cand != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (ice->num_remote_candidates >= ICE_MAX_CANDIDATES) {
        TINYRTC_LOG_WARN("ice_add_remote_candidate: too many remote candidates");
        return TINYRTC_ERROR;
    }

    ice_candidate_internal_t *internal = &ice->remote[ice->num_remote_candidates];
    ice_candidate_from_sdp(sdp_cand, internal);
    ice->num_remote_candidates++;

    /* Create check pairs with all local candidates */
    for (int i = 0; i < ice->num_local_candidates; i++) {
        if (ice->num_check_pairs < ICE_MAX_CANDIDATES * ICE_MAX_CANDIDATES) {
            ice_check_pair_t *pair = &ice->check_pairs[ice->num_check_pairs];
            pair->local = &ice->local[i];
            pair->remote = internal;
            pair->succeeded = false;
            pair->last_ping_ms = 0;
            pair->rtt = 0;
            ice->num_check_pairs++;
        }
    }

    TINYRTC_LOG_DEBUG("Added remote ICE candidate %s:%d",
        sdp_cand->ip, sdp_cand->port);

    return TINYRTC_OK;
}

int ice_process_packet(ice_session_t *ice, const uint8_t *data, size_t len)
{
    TINYRTC_CHECK(ice != NULL, 0);
    TINYRTC_CHECK(data != NULL, 0);

    /* Check if it's a STUN packet (magic cookie at offset 4) */
    if (len < 8) {
        return 1; /* Not STUN, it's media */
    }

    /* STUN magic cookie read from network byte order */
    uint32_t magic = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) |
                     ((uint32_t)data[6] << 8) | (uint32_t)data[7];
    if (magic != STUN_MAGIC_COOKIE) {
        return 1; /* Not STUN */
    }

    /* It's STUN - process it */
    char mapped_ip[64];
    uint16_t mapped_port;
    tinyrtc_error_t err = stun_process_response(ice, data, len, mapped_ip, sizeof(mapped_ip), &mapped_port);

    if (err == TINYRTC_OK && mapped_ip[0] != '\0') {
        /* Add server reflexive candidate */
        if (ice->num_local_candidates < ICE_MAX_CANDIDATES) {
            ice_candidate_internal_t *cand = &ice->local[ice->num_local_candidates];
            memset(cand, 0, sizeof(*cand));
            cand->type = ICE_CANDIDATE_TYPE_SRFLX;
            strncpy(cand->ip, mapped_ip, sizeof(cand->ip) - 1);
            cand->port = mapped_port;
            strcpy(cand->protocol, "udp");
            cand->is_ipv6 = strchr(mapped_ip, ':') != NULL;
            cand->priority = ice_calculate_priority(ICE_CANDIDATE_TYPE_SRFLX);
            cand->selected = false;
            ice->num_local_candidates++;

            TINYRTC_LOG_DEBUG("Added server reflexive candidate %s:%d", mapped_ip, mapped_port);

            /* Notify application via callback with new candidate */
            /* TODO: call pc->config.observer.on_ice_candidate */
        }
    }

    return 0; /* Handled as STUN */
}

void ice_check_connectivity(ice_session_t *ice, uint64_t now)
{
    if (ice == NULL) {
        return;
    }

    if (ice->state < 3) {
        /* Not started checking yet */
        return;
    }

    /* Send connectivity check pings to all unchecked pairs */
    for (int i = 0; i < ice->num_check_pairs; i++) {
        ice_check_pair_t *pair = &ice->check_pairs[i];
        if (!pair->succeeded && (pair->last_ping_ms == 0 ||
            (now - pair->last_ping_ms) > (uint64_t)(ICE_STUN_TIMEOUT_MS * ICE_MAX_RETRIES))) {

            /* TODO: Send connectivity check */
            pair->last_ping_ms = now;

            /* For now, just log */
            TINYRTC_LOG_DEBUG("Checking candidate pair: local=%s:%d remote=%s:%d",
                pair->local->ip, pair->local->port,
                pair->remote->ip, pair->remote->port);
        }
    }

    /* Check if we have a connected pair */
    for (int i = 0; i < ice->num_check_pairs; i++) {
        if (ice->check_pairs[i].succeeded && !ice->selected_pair) {
            ice->selected_pair = &ice->check_pairs[i];
            ice->selected_pair->local->selected = true;
            ice->state = 4; /* done */
            TINYRTC_LOG_INFO("ICE connected with pair %p RTT %d ms",
                ice->selected_pair, ice->selected_pair->rtt);

            /* Notify application connection state change */
            /* TODO: pc->observer.on_connection_state_change */
        }
    }
}

bool ice_is_connected(ice_session_t *ice)
{
    if (ice == NULL) {
        return false;
    }
    return ice->selected_pair != NULL && ice->selected_pair->succeeded;
}

void ice_candidate_to_sdp(const ice_candidate_internal_t *src,
                           sdp_candidate_t *dst)
{
    strcpy(dst->foundation, "tiny");
    dst->priority = src->priority;
    strcpy(dst->ip, src->ip);
    dst->port = src->port;
    if (src->type == ICE_CANDIDATE_TYPE_HOST) {
        strcpy(dst->type, "host");
    } else if (src->type == ICE_CANDIDATE_TYPE_SRFLX) {
        strcpy(dst->type, "srflx");
    } else {
        strcpy(dst->type, "relay");
    }
    strcpy(dst->protocol, src->protocol);
    dst->is_ipv6 = src->is_ipv6;
}

void ice_candidate_from_sdp(const sdp_candidate_t *src,
                             ice_candidate_internal_t *dst)
{
    dst->priority = src->priority;
    strcpy(dst->ip, src->ip);
    dst->port = src->port;
    if (strcmp(src->type, "host") == 0) {
        dst->type = ICE_CANDIDATE_TYPE_HOST;
    } else if (strcmp(src->type, "srflx") == 0) {
        dst->type = ICE_CANDIDATE_TYPE_SRFLX;
    } else {
        dst->type = ICE_CANDIDATE_TYPE_RELAY;
    }
    strcpy(dst->protocol, src->protocol);
    dst->is_ipv6 = src->is_ipv6;
    dst->selected = false;
}
