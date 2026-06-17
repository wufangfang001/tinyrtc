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
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "mbedtls/md.h"

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

static void stun_write64(uint8_t *p, uint64_t v)
{
    p[0] = (v >> 56) & 0xFF;
    p[1] = (v >> 48) & 0xFF;
    p[2] = (v >> 40) & 0xFF;
    p[3] = (v >> 32) & 0xFF;
    p[4] = (v >> 24) & 0xFF;
    p[5] = (v >> 16) & 0xFF;
    p[6] = (v >> 8) & 0xFF;
    p[7] = v & 0xFF;
}

static uint32_t stun_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }

    return ~crc;
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
    memcpy(ice->stun_server_transaction_id, hdr->transaction_id, sizeof(hdr->transaction_id));
    ice->stun_server_transaction_id_valid = true;

    /* Calculate total packet size */
    size_t total_len = sizeof(stun_header_t);

    /* Send to STUN server via the ICE UDP socket */
    struct sockaddr_in stun_addr;
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    char port_str[16];
    int gai_ret;

    memset(&stun_addr, 0, sizeof(stun_addr));
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(port_str, sizeof(port_str), "%u", server_port);

    gai_ret = getaddrinfo(server_addr, port_str, &hints, &result);
    if (gai_ret != 0 || result == NULL) {
        TINYRTC_LOG_ERROR("Failed to resolve STUN server %s:%u", server_addr, server_port);
        if (result != NULL) {
            freeaddrinfo(result);
        }
        return TINYRTC_ERROR_NETWORK;
    }

    memcpy(&stun_addr, result->ai_addr, sizeof(stun_addr));
    freeaddrinfo(result);

    int sent = sendto(ice->socket, buf, total_len, 0,
        (struct sockaddr *)&stun_addr, sizeof(stun_addr));

    if (sent < 0) {
        TINYRTC_LOG_ERROR("Failed to send STUN binding request: %s", strerror(errno));
        return TINYRTC_ERROR_NETWORK;
    }

    TINYRTC_LOG_INFO("STUN binding request sent to %s:%d, %d bytes",
        server_addr, server_port, (int)sent);

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
                    uint32_t ip = htonl(ip_xor ^ STUN_MAGIC_COOKIE);
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
                    uint32_t ip = htonl(stun_read32(p + 4));
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

tinyrtc_error_t stun_finalize_message(uint8_t *buffer, size_t *len, size_t buf_size,
                                      const char *password)
{
    const mbedtls_md_info_t *md_info;
    uint8_t hmac[20];
    uint16_t attr_type;
    uint16_t attr_len;
    uint32_t fingerprint;
    size_t message_len_for_hmac;

    TINYRTC_CHECK(buffer != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(len != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(password != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (*len + 24 + 8 > buf_size) {
        return TINYRTC_ERROR_MEMORY;
    }

    attr_type = htons(STUN_ATTR_MESSAGE_INTEGRITY);
    attr_len = htons(20);
    memcpy(buffer + *len, &attr_type, 2);
    memcpy(buffer + *len + 2, &attr_len, 2);
    memset(buffer + *len + 4, 0, 20);

    message_len_for_hmac = *len + 24;
    ((stun_header_t *)buffer)->length = htons((uint16_t)(message_len_for_hmac - sizeof(stun_header_t)));

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    if (md_info == NULL ||
        mbedtls_md_hmac(md_info,
                        (const unsigned char *)password,
                        strlen(password),
                        buffer,
                        message_len_for_hmac,
                        hmac) != 0) {
        return TINYRTC_ERROR;
    }

    memcpy(buffer + *len + 4, hmac, sizeof(hmac));
    *len = message_len_for_hmac;

    attr_type = htons(STUN_ATTR_FINGERPRINT);
    attr_len = htons(4);
    memcpy(buffer + *len, &attr_type, 2);
    memcpy(buffer + *len + 2, &attr_len, 2);

    ((stun_header_t *)buffer)->length = htons((uint16_t)(*len + 8 - sizeof(stun_header_t)));
    fingerprint = stun_crc32(buffer, *len + 4) ^ 0x5354554Eu;
    fingerprint = htonl(fingerprint);
    memcpy(buffer + *len + 4, &fingerprint, 4);
    *len += 8;

    return TINYRTC_OK;
}

size_t stun_create_binding_response(uint8_t *buffer, size_t buf_size,
                                      stun_header_t *request_header,
                                      const struct sockaddr_in *mapped_addr,
                                      const char *password)
{
    stun_header_t *resp = (stun_header_t *)buffer;
    size_t total_len;
    uint16_t attr_type;
    uint16_t attr_len;
    uint16_t xport;
    uint32_t xaddr;
    tinyrtc_error_t err;

    memset(buffer, 0, buf_size);

    /* Copy transaction ID from request */
    resp->type = htons(STUN_BINDING_RESPONSE);
    resp->length = htons(0); /* No attributes for a simple connectivity check response */
    resp->magic_cookie = htonl(STUN_MAGIC_COOKIE);

    /* Copy transaction ID matches request */
    for (int i = 0; i < 12; i++) {
        resp->transaction_id[i] = request_header->transaction_id[i];
    }

    total_len = sizeof(stun_header_t);

    if (mapped_addr != NULL && buf_size >= total_len + 12) {
        attr_type = htons(STUN_ATTR_XOR_MAPPED_ADDRESS);
        attr_len = htons(8);
        memcpy(buffer + total_len, &attr_type, 2);
        memcpy(buffer + total_len + 2, &attr_len, 2);
        buffer[total_len + 4] = 0;
        buffer[total_len + 5] = 0x01;
        xport = ntohs(mapped_addr->sin_port) ^ (uint16_t)(STUN_MAGIC_COOKIE >> 16);
        xport = htons(xport);
        memcpy(buffer + total_len + 6, &xport, 2);
        xaddr = ntohl(mapped_addr->sin_addr.s_addr) ^ STUN_MAGIC_COOKIE;
        xaddr = htonl(xaddr);
        memcpy(buffer + total_len + 8, &xaddr, 4);
        total_len += 12;
    }

    err = stun_finalize_message(buffer, &total_len, buf_size,
                                password != NULL ? password : "");
    if (err != TINYRTC_OK) {
        return 0;
    }

    TINYRTC_LOG_DEBUG("Created STUN binding response, size=%zu", total_len);
    return total_len;
}
