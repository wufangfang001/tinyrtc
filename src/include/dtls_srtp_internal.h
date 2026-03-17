/**
 * @file dtls_srtp_internal.h
 * @brief DTLS-SRTP internal header - structures and function declarations
 *
 * DTLS handles key exchange for SRTP, provides fingerprint verification.
 * SRTP handles encryption/authentication of RTP media packets.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_DTLS_SRTP_INTERNAL_H
#define TINYRTC_DTLS_SRTP_INTERNAL_H

#include "common.h"
#include "ice_internal.h"

#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * DTLS constants
 * ========================================================================== */

#define DTLS_FINGERPRINT_MAX_LEN 64

/* =============================================================================
 * DTLS role
 * ========================================================================== */

typedef enum {
    DTLS_ROLE_CLIENT = 0,
    DTLS_ROLE_SERVER = 1,
} dtls_role_t;

/* =============================================================================
 * DTLS context structure
 * ========================================================================== */

typedef struct dtls_context {
    mbedtls_ssl_context *ssl;           /* mbedtls SSL context */
    mbedtls_ssl_config *ctx;            /* mbedtls SSL configuration */
    mbedtls_x509_crt *cert;             /* Our certificate */
    mbedtls_pk_context *pkey;           /* Our private key */
    dtls_role_t role;
    char fingerprint[DTLS_FINGERPRINT_MAX_LEN * 3]; /* SHA-256 fingerprint in hex */
    bool handshake_complete;
    /* Store captured master secret for DTLS-SRTP key derivation
     * Compatible with both mbedtls 2.x and 3.x */
    unsigned char master_secret[48];
    bool master_secret_captured;
    /* Derived keys for SRTP */
    uint8_t client_master_key[16];
    uint8_t server_master_key[16];
    uint8_t client_salt[14];
    uint8_t server_salt[14];
} dtls_context_t;

/* =============================================================================
 * SRTP context structure
 * ========================================================================== */

typedef struct srtp_context {
    bool initialized;
    /* Key material derived from DTLS */
    uint8_t key[16];       /* Encryption key */
    uint8_t salt[14];       /* Salt */
    uint32_t roc;            /* Roll-over-counter */
} srtp_context_t;

/* =============================================================================
 * DTLS functions
 * ========================================================================== */

/**
 * @brief Initialize DTLS context
 *
 * @param role DTLS role (client/server) based on initiator
 * @return New DTLS context, NULL on error
 */
dtls_context_t *dtls_init(dtls_role_t role);

/**
 * @brief Destroy DTLS context
 *
 * @param dtls DTLS context to destroy
 */
void dtls_destroy(dtls_context_t *dtls);

/**
 * @brief Get certificate fingerprint in hex format
 *
 * @param dtls DTLS context
 * @param buf Output buffer for fingerprint
 * @param len Buffer length
 * @return Length of fingerprint on success
 */
int dtls_get_fingerprint(dtls_context_t *dtls, char *buf, size_t len);

/**
 * @brief Verify peer certificate fingerprint against expected
 *
 * @param dtls DTLS context
 * @param peer_fingerprint Peer fingerprint from SDP
 * @return true if fingerprint matches
 */
bool dtls_verify_fingerprint(dtls_context_t *dtls, const char *peer_fingerprint);

/**
 * @brief Process incoming DTLS data
 *
 * @param dtls DTLS context
 * @param data Incoming data
 * @param len Data length
 * @return TINYRTC_OK on success, error otherwise
 */
tinyrtc_error_t dtls_process_data(dtls_context_t *dtls, const uint8_t *data, size_t len);

/**
 * @brief Check if DTLS handshake is complete
 *
 * @param dtls DTLS context
 * @return true if handshake complete
 */
bool dtls_is_handshake_complete(dtls_context_t *dtls);

/**
 * @brief Derive SRTP keys after handshake completes
 *
 * @param dtls DTLS context
 * @param client_key Output client SRTP master key
 * @param client_salt Output client SRTP salt
 * @param server_key Output server SRTP master key
 * @param server_salt Output server SRTP salt
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t dtls_derive_srtp_keys(dtls_context_t *dtls,
                                         uint8_t client_key[16],
                                         uint8_t client_salt[14],
                                         uint8_t server_key[16],
                                         uint8_t server_salt[14]);

/* =============================================================================
 * SRTP functions
 * ========================================================================== */

/**
 * @brief Initialize SRTP context with key material from DTLS
 *
 * @param key Master key (16 bytes)
 * @param salt Salt (14 bytes)
 * @return New SRTP context
 */
srtp_context_t *srtp_init(const uint8_t *key, const uint8_t *salt);

/**
 * @brief Destroy SRTP context
 *
 * @param srtp SRTP context to destroy
 */
void srtp_destroy(srtp_context_t *srtp);

/**
 * @brief Encrypt RTP packet in place
 *
 * @param srtp SRTP context
 * @param packet RTP packet buffer
 * @param *len[in/out] On input: packet length, on output: extended length with auth tag
 * @param max_len Maximum buffer size
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t srtp_encrypt_packet(srtp_context_t *srtp,
                                     uint8_t *packet,
                                     size_t *len,
                                     size_t max_len);

/**
 * @brief Authenticate and decrypt RTP packet in place
 *
 * @param srtp SRTP context
 * @param packet RTP packet buffer
 * @param len Packet length including auth tag
 * @param[out] output_len Output plaintext length
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t srtp_decrypt_packet(srtp_context_t *srtp,
                                     uint8_t *packet,
                                     size_t len,
                                     size_t *output_len);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_DTLS_SRTP_INTERNAL_H */
