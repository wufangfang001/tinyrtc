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
#include "peer_connection_internal.h"
#include "tinyrtc/peer_connection.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <unistd.h>

/* STUN helper functions */
static uint16_t stun_read16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t stun_read32(const uint8_t *p)
{
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

static void stun_write16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

static void stun_write32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/* STUN magic cookie */
#define STUN_MAGIC_COOKIE 0x2112A442

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

static void ice_add_local_candidate(ice_session_t *ice,
                                    const char *ip,
                                    uint16_t port,
                                    uint32_t priority)
{
    ice_candidate_internal_t *cand;

    if (ice == NULL || ip == NULL || ice->num_local_candidates >= ICE_MAX_CANDIDATES) {
        return;
    }

    for (int i = 0; i < ice->num_local_candidates; i++) {
        if (strcmp(ice->local[i].ip, ip) == 0 && ice->local[i].port == port) {
            return;
        }
    }

    cand = &ice->local[ice->num_local_candidates];
    memset(cand, 0, sizeof(*cand));
    cand->type = ICE_CANDIDATE_TYPE_HOST;
    strncpy(cand->ip, ip, sizeof(cand->ip) - 1);
    cand->port = port;
    strcpy(cand->protocol, "udp");
    cand->is_ipv6 = strchr(ip, ':') != NULL;
    cand->priority = priority;
    cand->selected = false;
    ice->num_local_candidates++;
}

static void ice_add_check_pairs_for_local_candidate(ice_session_t *ice,
                                                    ice_candidate_internal_t *local)
{
    if (ice == NULL || local == NULL) {
        return;
    }

    for (int i = 0; i < ice->num_remote_candidates; i++) {
        if (ice->num_check_pairs >= ICE_MAX_CANDIDATES * ICE_MAX_CANDIDATES) {
            return;
        }
        ice_check_pair_t *pair = &ice->check_pairs[ice->num_check_pairs++];
        memset(pair, 0, sizeof(*pair));
        pair->local = local;
        pair->remote = &ice->remote[i];
    }
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

    ice->state = 1; /* gathering */

    /* When no STUN URL is provided (localhost testing), just create UDP socket and add host candidate */
    if (stun_url == NULL) {
        TINYRTC_LOG_INFO("No STUN server configured, using local host candidate only");
    } else {
        TINYRTC_LOG_DEBUG("Starting ICE candidate gathering from %s", stun_url);
    }

    /* Create UDP socket */
    int sock = aosl_hal_sk_socket(AOSL_AF_INET, AOSL_SOCK_DGRAM, AOSL_IPPROTO_UDP);
    if (sock < 0) {
        TINYRTC_LOG_ERROR("Failed to create UDP socket for ICE");
        ice->state = 2; /* gathered */
        ice->gathering_complete = true;
        return TINYRTC_ERROR_NETWORK;
    }

    /* Enable address reuse to avoid bind failures when restarting quickly */
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

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
    TINYRTC_LOG_INFO("ICE UDP socket created and bound successfully, fd=%d", sock);

    /* Get actual port we bound to */
    struct sockaddr_in bound_addr;
    socklen_t addr_len = sizeof(bound_addr);
    int ret_getname = getsockname((int)sock, (struct sockaddr *)&bound_addr, &addr_len);
    uint16_t local_port = ntohs(bound_addr.sin_port);

    {
        struct ifaddrs *ifaddr = NULL;
        struct ifaddrs *ifa;

        if (getifaddrs(&ifaddr) == 0) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                char ip[INET_ADDRSTRLEN];
                struct sockaddr_in *sin;

                if (ifa->ifa_addr == NULL || ifa->ifa_addr->sa_family != AF_INET) {
                    continue;
                }
                if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
                    continue;
                }

                sin = (struct sockaddr_in *)ifa->ifa_addr;
                if (inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip)) == NULL) {
                    continue;
                }
                ice_add_local_candidate(ice, ip, local_port,
                                        ICE_PRIORITY_HOST - (uint32_t)(ice->num_local_candidates * 10));
            }
            freeifaddrs(ifaddr);
        }
    }

    if (ice->num_local_candidates == 0) {
        ice_add_local_candidate(ice, "127.0.0.1", local_port, ICE_PRIORITY_HOST);
    }

    for (int i = 0; i < ice->num_local_candidates; i++) {
        TINYRTC_LOG_INFO("Added local host candidate %s:%d", ice->local[i].ip, ice->local[i].port);
    }

    /* If STUN URL is provided, send STUN binding request to get server-reflexive candidate */
    if (stun_url != NULL) {
        char stun_host[128];
        uint16_t stun_port;
        if (parse_stun_url(stun_url, stun_host, sizeof(stun_host), &stun_port)) {
            TINYRTC_LOG_INFO("Sending STUN binding request to %s:%d to get server-reflexive candidate",
                stun_host, stun_port);
            
            /* Send STUN binding request via stun module */
            tinyrtc_error_t stun_ret = stun_send_binding_request(ice, stun_host, stun_port);
            if (stun_ret != TINYRTC_OK) {
                TINYRTC_LOG_WARN("Failed to send STUN binding request: %d", stun_ret);
            }
        } else {
            TINYRTC_LOG_ERROR("Invalid STUN URL: %s", stun_url);
        }
    }

    ice->state = 2; /* gathered */
    ice->gathering_complete = true;
    TINYRTC_LOG_INFO("==========>> ICE candidate gathering complete, %d local candidates total, state=%d", ice->num_local_candidates, ice->state);
    return TINYRTC_OK;

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

    if (strcasecmp(sdp_cand->protocol, "udp") != 0) {
        TINYRTC_LOG_INFO("Skipping unsupported remote ICE candidate protocol %s for %s:%d",
            sdp_cand->protocol, sdp_cand->ip, sdp_cand->port);
        return TINYRTC_OK;
    }

    ice_candidate_internal_t *internal = &ice->remote[ice->num_remote_candidates];
    ice_candidate_from_sdp(sdp_cand, internal);
    ice->num_remote_candidates++;

    /* Create check pairs with all local candidates */
    for (int i = 0; i < ice->num_local_candidates; i++) {
        if (ice->num_check_pairs >= ICE_MAX_CANDIDATES * ICE_MAX_CANDIDATES) {
            break;
        }
        ice_check_pair_t *pair = &ice->check_pairs[ice->num_check_pairs++];
        memset(pair, 0, sizeof(*pair));
        pair->local = &ice->local[i];
        pair->remote = internal;
    }

    TINYRTC_LOG_DEBUG("Added remote ICE candidate %s:%d",
        sdp_cand->ip, sdp_cand->port);

    return TINYRTC_OK;
}

int ice_process_packet(ice_session_t *ice, const uint8_t *data, size_t len,
                       const struct sockaddr_in *source_addr)
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

    /* Read STUN message type */
    stun_header_t *hdr = (stun_header_t *)data;
    uint16_t msg_type = ntohs(hdr->type);

    if (msg_type == STUN_BINDING_REQUEST) {
        /* This is a connectivity check request from remote peer - send binding response */
        /* We need to send binding response back to confirm connectivity */

        uint8_t buffer[512];
        size_t resp_len = stun_create_binding_response(buffer, sizeof(buffer), hdr,
                                                       source_addr,
                                                       ice->pc->local_sdp.ice_pwd);
        if (source_addr != NULL) {
            int sent = sendto(ice->socket, buffer, resp_len, 0,
                (const struct sockaddr *)source_addr, sizeof(*source_addr));
            char remote_ip[INET_ADDRSTRLEN] = {0};

            inet_ntop(AF_INET, &source_addr->sin_addr, remote_ip, sizeof(remote_ip));
            TINYRTC_LOG_INFO("Sent STUN binding response to %s:%d, %d bytes",
                remote_ip, ntohs(source_addr->sin_port), sent);

            for (int i = 0; i < ice->num_check_pairs; i++) {
                if (ice->check_pairs[i].remote->port == ntohs(source_addr->sin_port) &&
                    strcmp(ice->check_pairs[i].remote->ip, remote_ip) == 0 &&
                    !ice->check_pairs[i].succeeded) {
                    ice->check_pairs[i].succeeded = true;
                    TINYRTC_LOG_INFO("Connectivity check succeeded (received request) from %s:%d",
                        remote_ip, ntohs(source_addr->sin_port));
                }
            }
        }

        return 0; /* Handled as STUN */
    }

    /* If it's STUN Binding Response - mark connectivity check succeeded */
    if (msg_type == STUN_BINDING_RESPONSE) {
        bool is_stun_server_response = false;
        bool matched_pair = false;

        if (ice->stun_server_transaction_id_valid &&
            memcmp(hdr->transaction_id, ice->stun_server_transaction_id, sizeof(hdr->transaction_id)) == 0) {
            is_stun_server_response = true;
            TINYRTC_LOG_INFO("Received STUN Binding Response from STUN server");
        }

        if (!is_stun_server_response) {
            for (int i = 0; i < ice->num_check_pairs; i++) {
                if (ice->check_pairs[i].last_transaction_id_valid &&
                    memcmp(hdr->transaction_id, ice->check_pairs[i].last_transaction_id,
                           sizeof(hdr->transaction_id)) == 0) {
                    ice->check_pairs[i].succeeded = true;
                    matched_pair = true;
                    TINYRTC_LOG_INFO("Connectivity check succeeded for pair %d", i);
                    break;
                }
            }
            if (!matched_pair) {
                TINYRTC_LOG_WARN("Received STUN Binding Response with unknown transaction ID");
            }
        }
    }

    /* Process STUN response for mapped address (from STUN server) */
    char mapped_ip[64];
    uint16_t mapped_port;
    mapped_ip[0] = '\0';
    tinyrtc_error_t err = stun_process_response(ice, data, len, mapped_ip, sizeof(mapped_ip), &mapped_port);

    /* If we got mapped address from STUN server, add server reflexive candidate */
    if (err == TINYRTC_OK && mapped_ip[0] != '\0') {
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
            ice_add_check_pairs_for_local_candidate(ice, cand);

            TINYRTC_LOG_INFO("Added server reflexive candidate %s:%d from STUN server", mapped_ip, mapped_port);

            /* Notify application via callback with new candidate */
            if (ice->pc != NULL && ice->pc->config.observer.on_ice_candidate != NULL) {
                sdp_candidate_t sdp_cand;
                tinyrtc_ice_candidate_t out_cand;

                memset(&sdp_cand, 0, sizeof(sdp_cand));
                ice_candidate_to_sdp(cand, &sdp_cand);
                out_cand.foundation = sdp_cand.foundation;
                out_cand.priority = sdp_cand.priority;
                out_cand.ip = sdp_cand.ip;
                out_cand.port = sdp_cand.port;
                out_cand.type = sdp_cand.type;
                out_cand.protocol = sdp_cand.protocol;
                out_cand.is_ipv6 = sdp_cand.is_ipv6;
                ice->pc->config.observer.on_ice_candidate(ice->pc->config.observer.user_data, &out_cand);
            }
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
        /* Not started checking yet - check if we can start now */
        if (ice->state == 2) {
            /* Gathering complete, now start checking */
            ice->state = 3; /* checking */
            TINYRTC_LOG_INFO("ICE gathering complete, starting connectivity checks (%d check pairs)",
                ice->num_check_pairs);
        } else {
            /* Still gathering, not ready to check */
            return;
        }
    }

    /* Send connectivity check pings to all unchecked pairs */
    int sent_this_tick = 0;
    for (int i = 0; i < ice->num_check_pairs; i++) {
        ice_check_pair_t *pair = &ice->check_pairs[i];
        if (!pair->succeeded && (pair->last_ping_ms == 0 ||
            (now - pair->last_ping_ms) > (uint64_t)(ICE_STUN_TIMEOUT_MS * ICE_MAX_RETRIES))) {

            pair->last_ping_ms = now;

            /* Send connectivity check request (STUN binding) */
            struct sockaddr_in remote_addr;
            memset(&remote_addr, 0, sizeof(remote_addr));
            remote_addr.sin_family = AF_INET;
            inet_pton(AF_INET, pair->remote->ip, &remote_addr.sin_addr);
            remote_addr.sin_port = htons(pair->remote->port);

            /* Create STUN binding request */
            uint8_t buffer[512];
            size_t len = 0;

            stun_header_t *hdr = (stun_header_t *)buffer;
            memset(buffer, 0, sizeof(buffer));
            stun_write16((uint8_t *)&hdr->type, STUN_BINDING_REQUEST);
            stun_write32((uint8_t *)&hdr->magic_cookie, STUN_MAGIC_COOKIE);

            /* Generate random transaction ID using current time */
            uint64_t now_stun = (uint64_t)aosl_time_ms();
            hdr->transaction_id[0] = (now_stun >> 56) & 0xFF;
            hdr->transaction_id[1] = (now_stun >> 48) & 0xFF;
            hdr->transaction_id[2] = (now_stun >> 40) & 0xFF;
            hdr->transaction_id[3] = (now_stun >> 32) & 0xFF;
            hdr->transaction_id[4] = (now_stun >> 24) & 0xFF;
            hdr->transaction_id[5] = (now_stun >> 16) & 0xFF;
            hdr->transaction_id[6] = (now_stun >> 8) & 0xFF;
            hdr->transaction_id[7] = now_stun & 0xFF;
            uint64_t extra = now_stun * 31337;
            hdr->transaction_id[8] = (extra >> 56) & 0xFF;
            hdr->transaction_id[9] = (extra >> 48) & 0xFF;
            hdr->transaction_id[10] = (extra >> 40) & 0xFF;
            hdr->transaction_id[11] = (extra >> 32) & 0xFF;
            memcpy(pair->last_transaction_id, hdr->transaction_id, sizeof(hdr->transaction_id));
            pair->last_transaction_id_valid = true;

            len = sizeof(stun_header_t);

            /* ======================================================================
             * Add USERNAME attribute (required by ICE RFC 8445)
             * Format: local_ufrag:remote_ufrag
             * ====================================================================== */
            char username[256];
            snprintf(username, sizeof(username), "%s:%s",
                ice->pc->remote_sdp.ice_ufrag,
                ice->pc->local_sdp.ice_ufrag);
            size_t username_len = strlen(username);

            /* Attribute header: type (2 bytes) + length (2 bytes) */
            uint16_t attr_type = htons(0x0006);  /* STUN_ATTR_USERNAME */
            uint16_t attr_len = htons((uint16_t)username_len);

            memcpy(buffer + len, &attr_type, 2);
            len += 2;
            memcpy(buffer + len, &attr_len, 2);
            len += 2;

            /* Attribute value */
            memcpy(buffer + len, username, username_len);
            len += username_len;

            /* Padding to 4-byte boundary */
            size_t padding = (4 - (username_len % 4)) % 4;
            if (padding > 0) {
                memset(buffer + len, 0, padding);
                len += padding;
            }

            /* ======================================================================
             * Add ICE-CONTROLLING / ICE-CONTROLLED attribute
             * ====================================================================== */
            uint64_t tie_breaker = now_stun;
            attr_type = htons(ice->pc->config.is_initiator ? 0x802A : 0x8029);
            attr_len = htons(8);

            memcpy(buffer + len, &attr_type, 2);
            len += 2;
            memcpy(buffer + len, &attr_len, 2);
            len += 2;

            /* Write 64-bit tie-breaker value (network byte order) */
            for (int i = 0; i < 8; i++) {
                buffer[len + i] = (tie_breaker >> (56 - 8 * i)) & 0xFF;
            }
            len += 8;

            /* ======================================================================
             * Add PRIORITY attribute
             * ====================================================================== */
            attr_type = htons(0x0024);  /* STUN_ATTR_PRIORITY */
            attr_len = htons(4);
            uint32_t priority = htonl(pair->local->priority);

            memcpy(buffer + len, &attr_type, 2);
            len += 2;
            memcpy(buffer + len, &attr_len, 2);
            len += 2;
            memcpy(buffer + len, &priority, 4);
            len += 4;

            if (stun_finalize_message(buffer, &len, sizeof(buffer), ice->pc->remote_sdp.ice_pwd) != TINYRTC_OK) {
                TINYRTC_LOG_WARN("Failed to finalize ICE connectivity check STUN message");
                continue;
            }

            TINYRTC_LOG_DEBUG("  STUN username: %s", username);

            /* Send to remote candidate via our UDP socket */
            int sent = sendto(ice->socket, buffer, len, 0,
                (struct sockaddr *)&remote_addr, sizeof(remote_addr));

            TINYRTC_LOG_DEBUG("Sending STUN connectivity check: local=%s:%d remote=%s:%d, sent %d bytes",
                pair->local->ip, pair->local->port,
                pair->remote->ip, pair->remote->port, (int)sent);
            fflush(stdout);
        }
    }

    /* Check if we have a connected pair */
    for (int i = 0; i < ice->num_check_pairs; i++) {
        if (ice->check_pairs[i].succeeded && !ice->selected_pair) {
            ice->selected_pair = &ice->check_pairs[i];
            ice->selected_pair->local->selected = true;
            ice->state = 4; /* done */
            TINYRTC_LOG_INFO("ICE connected with pair local=%s:%d remote=%s:%d RTT %d ms",
                ice->check_pairs[i].local->ip, ice->check_pairs[i].local->port,
                ice->check_pairs[i].remote->ip, ice->check_pairs[i].remote->port, ice->selected_pair->rtt);
            fflush(stdout);
            /* Notify application connection state change is done at pc level after */
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
