/**
 * @file stun.c
 * @brief STUN (Session Traversal Utilities for NAT) client implementation
 *
 * Implements STUN binding requests for getting public mapped address.
 * Follows RFC 5389.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "ice_internal.h"

#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

/* stun_header_t and constants already defined in ice_internal.h */

/* =============================================================================
 * Local helpers
 * ========================================================================== */

/**
 * Get 16-bit from network byte order
 */
static uint16_t stun_read16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

/**
 * Get 32-bit from network byte order
 */
static uint32_t stun_read32(const uint8_t *p)
{
    return (uint32_t)((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]);
}

/**
 * Write 16-bit to network byte order
 */
static void stun_write16(uint8_t *p, uint16_t v)
{
    p[0] = (v >> 8) & 0xFF;
    p[1] = v & 0xFF;
}

/**
 * Write 32-bit to network byte order
 */
static void stun_write32(uint8_t *p, uint32_t v)
{
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >> 8) & 0xFF;
    p[3] = v & 0xFF;
}

/**
 * Validate STUN packet
 */
static bool stun_validate_header(const stun_header_t *hdr)
{
    if (hdr->magic_cookie != htonl(STUN_MAGIC_COOKIE)) {
        return false;
    }
    /* Check length is reasonable */
    if (ntohs(hdr->length) > 1500) {
        return false;
    }
    return true;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

tinyrtc_error_t stun_send_binding_request(ice_session_t *ice,
                                          const char *server_addr,
                                          uint16_t server_port)
{
    TINYRTC_CHECK(ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(server_addr != NULL, TINYRTC_ERROR_INVALID_ARG);

    /* Create STUN packet */
    uint8_t buf[1024];
    stun_header_t *hdr = (stun_header_t *)buf;

    memset(buf, 0, sizeof(buf));
    stun_write16((uint8_t *)&hdr->type, STUN_BINDING_REQUEST);
    stun_write16((uint8_t *)&hdr->length, 0); /* No attributes yet */
    stun_write32((uint8_t *)&hdr->magic_cookie, STUN_MAGIC_COOKIE);

    /* Generate random transaction ID */
    uint64_t now = (uint64_t)aosl_time_ms();
    /* Use current time as random (good enough for now) */
    hdr->transaction_id[0] = (now >> 56) & 0xFF;
    hdr->transaction_id[1] = (now >> 48) & 0xFF;
    hdr->transaction_id[2] = (now >> 40) & 0xFF;
    hdr->transaction_id[3] = (now >> 32) & 0xFF;
    hdr->transaction_id[4] = (now >> 24) & 0xFF;
    hdr->transaction_id[5] = (now >> 16) & 0xFF;
    hdr->transaction_id[6] = (now >> 8) & 0xFF;
    hdr->transaction_id[7] = now & 0xFF;
    /* More random bits from another source */
    uint64_t extra = now * 31337;
    hdr->transaction_id[8] = (extra >> 56) & 0xFF;
    hdr->transaction_id[9] = (extra >> 48) & 0xFF;
    hdr->transaction_id[10] = (extra >> 40) & 0xFF;
    hdr->transaction_id[11] = (extra >> 32) & 0xFF;

    /* Calculate total packet size */
    size_t total_len = sizeof(stun_header_t);

    /* Send to STUN server */
    /* TODO: actual send via socket */
    /* For now, we just build the packet */

    TINYRTC_LOG_DEBUG("STUN binding request created to %s:%d", server_addr, server_port);

    /* When we have the socket implemented, send here */

    return TINYRTC_OK;
}

tinyrtc_error_t stun_process_response(ice_session_t *ice,
                                       const uint8_t *data, size_t len,
                                       char *mapped_addr, size_t mapped_addr_len,
                                       uint16_t *mapped_port)
{
    TINYRTC_CHECK(ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(data != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(mapped_addr != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(mapped_port != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (len < sizeof(stun_header_t)) {
        TINYRTC_LOG_ERROR("STUN response too short");
        return TINYRTC_ERROR;
    }

    const stun_header_t *hdr = (const stun_header_t *)data;
    if (!stun_validate_header(hdr)) {
        TINYRTC_LOG_ERROR("Invalid STUN header");
        return TINYRTC_ERROR;
    }

    uint16_t msg_type = ntohs(hdr->type);
    uint16_t msg_len = ntohs(hdr->length);

    if (msg_type != STUN_BINDING_RESPONSE) {
        if (msg_type == STUN_BINDING_ERROR_RESPONSE) {
            TINYRTC_LOG_ERROR("STUN binding error response");
            return TINYRTC_ERROR;
        }
        TINYRTC_LOG_ERROR("Unexpected STUN message type %d", msg_type);
        return TINYRTC_ERROR;
    }

    /* Parse attributes */
    const uint8_t *p = data + sizeof(stun_header_t);
    const uint8_t *end = data + sizeof(stun_header_t) + msg_len;

    bool found_mapped = false;

    while (p < end) {
        if (p + 4 > end) break;

        uint16_t attr_type = stun_read16(p);
        uint16_t attr_len = stun_read16(p + 2);
        p += 4;

        /* Align to 4 bytes */
        size_t padded_len = (attr_len + 3) & ~3;

        if (p + padded_len > end) break;

        switch (attr_type) {
            case STUN_ATTR_XOR_MAPPED_ADDRESS:
            {
                /* XOR-Mapped-Address has the address XORed with magic cookie */
                if (attr_len < 4) break;

                uint8_t family = p[1];
                uint16_t port_xor = stun_read16(p + 2);
                *mapped_port = (port_xor ^ (STUN_MAGIC_COOKIE >> 16)) & 0xFFFF;

                if (family == 0x01) { /* IPv4 */
                    if (attr_len != 8) break;
                    uint32_t ip_xor = stun_read32(p + 4);
                    uint32_t ip = ip_xor ^ STUN_MAGIC_COOKIE;
                    if (mapped_addr_len >= INET_ADDRSTRLEN) {
                        inet_ntop(AF_INET, &ip, mapped_addr, (socklen_t)mapped_addr_len);
                        found_mapped = true;
                    }
                } else if (family == 0x02) { /* IPv6 */
                    /* TODO: IPv6 support */
                    TINYRTC_LOG_WARN("IPv6 STUN address not supported yet");
                }
                break;
            }

            case STUN_ATTR_MAPPED_ADDRESS:
            {
                /* Legacy Mapped-Address */
                if (attr_len < 4) break;

                uint8_t family = p[1];
                *mapped_port = stun_read16(p + 2);

                if (family == 0x01) { /* IPv4 */
                    if (attr_len != 8) break;
                    uint32_t ip = stun_read32(p + 4);
                    if (mapped_addr_len >= INET_ADDRSTRLEN) {
                        inet_ntop(AF_INET, &ip, mapped_addr, (socklen_t)mapped_addr_len);
                        found_mapped = true;
                    }
                }
                break;
            }

            default:
                /* Ignore other attributes */
                break;
        }

        p += padded_len;
    }

    if (!found_mapped) {
        TINYRTC_LOG_ERROR("STUN response has no mapped address");
        return TINYRTC_ERROR;
    }

    TINYRTC_LOG_DEBUG("STUN response got mapped address %s:%u", mapped_addr, *mapped_port);
    return TINYRTC_OK;
}

size_t stun_create_binding_response(uint8_t *buffer, size_t buf_size,
                                      stun_header_t *request_header)
{
    stun_header_t *resp = (stun_header_t *)buffer;
    memset(buffer, 0, buf_size);

    /* Copy transaction ID from request */
    resp->type = htons(STUN_BINDING_RESPONSE);
    resp->length = htons(0); /* No attributes for a simple connectivity check response */
    resp->magic_cookie = htonl(STUN_MAGIC_COOKIE);

    /* Copy transaction ID matches request */
    for (int i = 0; i < 12; i++) {
        resp->transaction_id[i] = request_header->transaction_id[i];
    }

    size_t total_len = sizeof(stun_header_t);
    resp->length = htons(0);

    TINYRTC_LOG_DEBUG("Created STUN binding response, size=%zu", total_len);
    return total_len;
}
