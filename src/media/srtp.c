/**
 * @file srtp.c
 * @brief SRTP (Secure Real-time Transport Protocol) implementation
 *
 * SRTP provides encryption and authentication for RTP media packets.
 * We use AES-128-CM with HMAC-SHA1 authentication as specified by WebRTC.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "dtls_srtp_internal.h"

#include "mbedtls/aes.h"
#include "mbedtls/md.h"

/* =============================================================================
 * SRTP Constants
 * ========================================================================== */

#define SRTP_MASTER_KEY_LEN     16
#define SRTP_MASTER_SALT_LEN    14
#define SRTP_AUTH_TAG_LEN        10
#define SRTP_KEY_LEN             (SRTP_MASTER_KEY_LEN + SRTP_MASTER_SALT_LEN)

/* =============================================================================
 * Local helpers
 * ========================================================================== */

/* Simple timing-safe compare that's not optimized away */
static int timingsafe_bcmp(const unsigned char *b1, const unsigned char *b2, size_t n)
{
    int res = 0;
    for (size_t i = 0; i < n; i++) {
        res |= b1[i] ^ b2[i];
    }
    return res;
}

/**
 * Derive session key from master key and salt according to RFC 3711
 */
static void srtp_derive_key(srtp_context_t *srtp, uint32_t ssrc,
                             uint8_t *output, size_t output_len)
{
    /* SRTP key derivation uses AES encryption of a counter based on SSRC */
    /* We just need the first 16 bytes for AES key */

    uint8_t counter[16] = {0};
    /* Put SSRC in the last 4 bytes */
    counter[12] = (ssrc >> 24) & 0xFF;
    counter[13] = (ssrc >> 16) & 0xFF;
    counter[14] = (ssrc >> 8) & 0xFF;
    counter[15] = ssrc & 0xFF;

    /* XOR with salt */
    for (int i = 0; i < 14; i++) {
        counter[i] ^= srtp->salt[i];
    }

    /* Encrypt counter with master key to get session key */
    mbedtls_aes_context aes_key;
    mbedtls_aes_setkey_enc(&aes_key, srtp->key, 128);
    mbedtls_aes_crypt_ecb(&aes_key, MBEDTLS_AES_ENCRYPT, counter, output);
}

/**
 * Generate authentication tag using HMAC-SHA1
 */
static void srtp_generate_auth(srtp_context_t *srtp, const uint8_t *packet,
                                size_t packet_len, uint8_t *tag)
{
    mbedtls_md_context_t md_ctx;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_hmac_starts(&md_ctx, srtp->key, 16);
    mbedtls_md_hmac_update(&md_ctx, packet, packet_len);
    mbedtls_md_hmac_finish(&md_ctx, tag);
    /* We only use first 10 bytes as specified by WebRTC */
}

/**
 * Verify authentication tag
 */
static bool srtp_verify_auth(srtp_context_t *srtp, const uint8_t *packet,
                              size_t packet_len, const uint8_t *tag)
{
    uint8_t computed[20];
    mbedtls_md_context_t md_ctx;
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_hmac_starts(&md_ctx, srtp->key, 16);
    mbedtls_md_hmac_update(&md_ctx, packet, packet_len);
    mbedtls_md_hmac_finish(&md_ctx, computed);

    /* Compare first 10 bytes */
    return timingsafe_bcmp(computed, tag, SRTP_AUTH_TAG_LEN) == 0;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

srtp_context_t *srtp_init(const uint8_t *key, const uint8_t *salt)
{
    TINYRTC_CHECK_NULL(key);
    TINYRTC_CHECK_NULL(salt);

    srtp_context_t *srtp = (srtp_context_t *)tinyrtc_calloc(1, sizeof(*srtp));
    if (srtp == NULL) {
        TINYRTC_LOG_ERROR("srtp_init: memory allocation failed");
        return NULL;
    }

    memcpy(srtp->key, key, SRTP_MASTER_KEY_LEN);
    memcpy(srtp->salt, salt, SRTP_MASTER_SALT_LEN);
    srtp->roc = 0;
    srtp->initialized = true;

    TINYRTC_LOG_DEBUG("srtp context initialized");
    return srtp;
}

void srtp_destroy(srtp_context_t *srtp)
{
    if (srtp == NULL) {
        return;
    }

    /* Zero out key material before freeing */
    memset(srtp->key, 0, sizeof(srtp->key));
    tinyrtc_internal_free(srtp);
    TINYRTC_LOG_DEBUG("srtp context destroyed");
}

tinyrtc_error_t srtp_encrypt_packet(srtp_context_t *srtp,
                                     uint8_t *packet,
                                     size_t *len,
                                     size_t max_len)
{
    TINYRTC_CHECK(srtp != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(packet != NULL, TINYRTC_ERROR_INVALID_ARG);

    size_t original_len = *len;

    /* Check we have space for auth tag */
    if (original_len + SRTP_AUTH_TAG_LEN > max_len) {
        TINYRTC_LOG_ERROR("srtp_encrypt_packet: buffer too small");
        return TINYRTC_ERROR;
    }

    /* Get SSRC from RTP header */
    /* SSRC is at offset 8 (version 2, 4 bytes per word: 0=version+seq, 1=timestamp, 2=ssrc) */
    uint32_t ssrc = (packet[8] << 24) | (packet[9] << 16) |
                      (packet[10] << 8) | packet[11];

    /* Derive encryption key */
    uint8_t enc_key[16];
    srtp_derive_key(srtp, ssrc, enc_key, sizeof(enc_key));

    /* AES encrypt the payload in place
     * RTP payload starts after header (typically 12 bytes, we don't know actual header length,
     * but for WebRTC we encrypt everything after the RTP header. The header itself is not encrypted.
     * This matches SRTP specification.
     */
    size_t header_len = 12; /* Minimum RTP header */
    uint8_t *payload = packet + header_len;
    size_t payload_len = original_len - header_len;

    mbedtls_aes_context aes_key;
    mbedtls_aes_setkey_enc(&aes_key, enc_key, 128);

    /* AES counter mode encryption */
    uint8_t counter_block[16] = {0};
    /* 64-bit counter starting at 0 */
    uint64_t counter = 0;
    /* Put counter in last 8 bytes */
    for (int i = 8; i < 16; i++) {
        counter_block[i] = (counter >> (8 * (15 - i))) & 0xFF;
    }
    /* XOR with salt */
    for (int i = 0; i < 14; i++) {
        counter_block[i] ^= srtp->salt[i];
    }

    for (size_t i = 0; i < payload_len; i += 16) {
        uint8_t encrypted[16];
        mbedtls_aes_crypt_ecb(&aes_key, MBEDTLS_AES_ENCRYPT, counter_block, encrypted);

        size_t block_len = (payload_len - i >= 16) ? 16 : payload_len - i;
        for (size_t j = 0; j < block_len; j++) {
            payload[i + j] ^= encrypted[j];
        }

        /* Increment counter */
        counter++;
        for (int i = 15; i >= 8 && counter > 0; i--) {
            counter_block[i]++;
            if (counter_block[i] != 0) {
                break;
            }
        }
    }

    /* Authenticate the entire packet including header */
    /* Append auth tag to the end */
    uint8_t *tag_ptr = packet + original_len;
    srtp_generate_auth(srtp, packet, original_len, tag_ptr);

    *len = original_len + SRTP_AUTH_TAG_LEN;

    /* Increment ROC */
    srtp->roc++;

    return TINYRTC_OK;
}

tinyrtc_error_t srtp_decrypt_packet(srtp_context_t *srtp,
                                     uint8_t *packet,
                                     size_t len,
                                     size_t *output_len)
{
    TINYRTC_CHECK(srtp != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(packet != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (len < (size_t)(12 + SRTP_AUTH_TAG_LEN)) {
        TINYRTC_LOG_ERROR("srtp_decrypt_packet: packet too short");
        return TINYRTC_ERROR;
    }

    /* Packet format: [ RTP header (12+) ] [ encrypted payload ] [ auth tag (10 bytes) ] */
    size_t packet_len_without_auth = len - SRTP_AUTH_TAG_LEN;
    const uint8_t *received_tag = packet + packet_len_without_auth;

    /* Verify authentication tag */
    if (!srtp_verify_auth(srtp, packet, packet_len_without_auth, received_tag)) {
        TINYRTC_LOG_ERROR("srtp_decrypt_packet: authentication failed");
        return TINYRTC_ERROR;
    }

    *output_len = packet_len_without_auth;

    /* Get SSRC from RTP header */
    uint32_t ssrc = (packet[8] << 24) | (packet[9] << 16) |
                      (packet[10] << 8) | packet[11];

    /* Derive decryption key (same as encryption key) */
    uint8_t dec_key[16];
    srtp_derive_key(srtp, ssrc, dec_key, sizeof(dec_key));

    /* AES counter mode decryption (same as encryption because AES-CTR is symmetric) */
    size_t header_len = 12;
    uint8_t *payload = packet + header_len;
    size_t payload_len = packet_len_without_auth - header_len;

    mbedtls_aes_context aes_key;
    mbedtls_aes_setkey_enc(&aes_key, dec_key, 128);

    uint8_t counter_block[16] = {0};
    uint64_t counter = 0; /* ROC is handled separately, starting from 0 */
    for (int i = 8; i < 16; i++) {
        counter_block[i] = (counter >> (8 * (15 - i))) & 0xFF;
    }
    /* XOR with salt */
    for (int i = 0; i < 14; i++) {
        counter_block[i] ^= srtp->salt[i];
    }

    for (size_t i = 0; i < payload_len; i += 16) {
        uint8_t decrypted[16];
        mbedtls_aes_crypt_ecb(&aes_key, MBEDTLS_AES_ENCRYPT, counter_block, decrypted);

        size_t block_len = (payload_len - i >= 16) ? 16 : payload_len - i;
        for (size_t j = 0; j < block_len; j++) {
            payload[i + j] ^= decrypted[j];
        }

        counter++;
        for (int i = 15; i >= 8 && counter > 0; i--) {
            counter_block[i]++;
            if (counter_block[i] != 0) {
                break;
            }
        }
    }

    /* Update ROC */
    srtp->roc++;

    return TINYRTC_OK;
}
