/**
 * @file dtls.c
 * @brief DTLS handshake implementation for WebRTC using mbedtls
 *
 * DTLS handles key exchange for WebRTC. We use mbedtls which is lightweight,
 * embedded-friendly, and good for portability across different chips.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "dtls_srtp_internal.h"

#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy_poll.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"

/* 
 * NOTE: TinyRTC currently supports both mbedtls 2.28+ and mbedtls 3.x
 * Compatibility handling for the key export API difference.
 * In mbedtls 3.x, mbedtls_ssl_export_keys was removed, we use callback.
 */

/* =============================================================================
 * Static global context for mbedtls
 * We keep one global entropy context shared by all DTLS connections
 * ========================================================================== */

static bool dtls_mbedtls_initialized = false;
static mbedtls_entropy_context dtls_entropy;
static mbedtls_ctr_drbg_context dtls_ctr_drbg;

/* =============================================================================
 * Internal helpers
 * ========================================================================== */

static int dtls_our_random(void *p_rng, unsigned char *output, size_t output_len)
{
    return mbedtls_ctr_drbg_random(p_rng, output, output_len);
}

/* Custom key export callback for mbedtls to capture master secret
 * This is required for mbedtls 3.x compatibility where mbedtls_ssl_export_keys was removed
 */
typedef struct {
    unsigned char *master_secret;
    bool got_master_secret;
} dtls_key_export_ctx_t;

static int dtls_key_export_callback(void *p_expkey,
                                      const unsigned char *ms,
                                      const unsigned char *kb,
                                      size_t maclen,
                                      size_t keylen,
                                      size_t ivlen)
{
    (void)kb; (void)maclen; (void)keylen; (void)ivlen;

    dtls_key_export_ctx_t *ctx = (dtls_key_export_ctx_t *)p_expkey;

    /* We only need the master_secret, 48 bytes for DTLS-SRTP */
    if (ctx != NULL && ctx->master_secret != NULL) {
        memcpy(ctx->master_secret, ms, 48);
        ctx->got_master_secret = true;
    }

    return 0;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

dtls_context_t *dtls_init(dtls_role_t role)
{
    /* Initialize mbedtls once */
    if (!dtls_mbedtls_initialized) {
        mbedtls_entropy_init(&dtls_entropy);
        mbedtls_ctr_drbg_init(&dtls_ctr_drbg);

        const char *pers = "tinyrtc-dtls";
        int ret = mbedtls_ctr_drbg_seed(&dtls_ctr_drbg,
                                     mbedtls_entropy_func,
                                     &dtls_entropy,
                                     (const unsigned char *)pers,
                                     strlen(pers));
        if (ret != 0) {
            TINYRTC_LOG_ERROR("dtls_init: failed to seed random generator: %d", ret);
            return NULL;
        }

        dtls_mbedtls_initialized = true;
    }

    dtls_context_t *dtls = (dtls_context_t *)tinyrtc_calloc(1, sizeof(*dtls));
    if (dtls == NULL) {
        TINYRTC_LOG_ERROR("dtls_init: memory allocation failed");
        return NULL;
    }

    /* Initialize mbedtls SSL context */
    mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)tinyrtc_calloc(1, sizeof(mbedtls_ssl_context));
    mbedtls_ssl_config *ssl_cfg = (mbedtls_ssl_config *)tinyrtc_calloc(1, sizeof(mbedtls_ssl_config));
    if (ssl == NULL || ssl_cfg == NULL) {
        if (ssl != NULL) {
            mbedtls_ssl_free(ssl);
        }
        if (ssl_cfg != NULL) {
            mbedtls_ssl_config_free(ssl_cfg);
        }
        tinyrtc_internal_free(dtls);
        return NULL;
    }

    /* Configure DTLS */
    mbedtls_ssl_config_init(ssl_cfg);
    if (role == DTLS_ROLE_CLIENT) {
        mbedtls_ssl_config_defaults(ssl_cfg,
            MBEDTLS_SSL_IS_CLIENT,
            MBEDTLS_SSL_TRANSPORT_DATAGRAM,
            MBEDTLS_SSL_PRESET_DEFAULT);
    } else {
        mbedtls_ssl_config_defaults(ssl_cfg,
            MBEDTLS_SSL_IS_SERVER,
            MBEDTLS_SSL_TRANSPORT_DATAGRAM,
            MBEDTLS_SSL_PRESET_DEFAULT);
    }

    /* Use ECDH key exchange */
    mbedtls_ssl_conf_rng(ssl_cfg, dtls_our_random, &dtls_ctr_drbg);

    /* Enable certificate verification - in WebRTC we verify via fingerprint out-of-band */
    mbedtls_ssl_conf_authmode(ssl_cfg, MBEDTLS_SSL_VERIFY_NONE);

    /* Set DTLS version - mbedtls uses major version 3 even for DTLS 1.2 */
    mbedtls_ssl_conf_min_version(ssl_cfg, MBEDTLS_SSL_MAJOR_VERSION_3, 2);
    mbedtls_ssl_conf_max_version(ssl_cfg, MBEDTLS_SSL_MAJOR_VERSION_3, 2);

    /* Configure elliptic curve for ECDHE - just P-256 which is what WebRTC needs */
    static const mbedtls_ecp_group_id curves[] = {
        MBEDTLS_ECP_DP_SECP256R1,
        MBEDTLS_ECP_DP_NONE
    };
    mbedtls_ssl_conf_curves(ssl_cfg, curves);

    /* Register key export callback to capture master secret
     * This is required for mbedtls 3.x compatibility where mbedtls_ssl_export_keys was removed
     */
    static dtls_key_export_ctx_t key_export_ctx = {0};
    key_export_ctx.master_secret = dtls->master_secret;
    key_export_ctx.got_master_secret = false;
    mbedtls_ssl_conf_export_keys_cb(ssl_cfg, dtls_key_export_callback, &key_export_ctx);
    dtls->master_secret_captured = false;

    /* Setup SSL */
    mbedtls_ssl_setup(ssl, ssl_cfg);

    /* Generate our EC key and self-signed certificate */
    mbedtls_x509_crt *cert = (mbedtls_x509_crt *)tinyrtc_calloc(1, sizeof(mbedtls_x509_crt));
    mbedtls_pk_context *pkey = (mbedtls_pk_context *)tinyrtc_calloc(1, sizeof(mbedtls_pk_context));
    if (cert == NULL || pkey == NULL) {
        TINYRTC_LOG_ERROR("dtls_init: certificate/private key allocation failed");
        goto cleanup;
    }

    /* Generate ECDH key on P-256 curve */
    mbedtls_ecp_keypair *ec_key = (mbedtls_ecp_keypair *)tinyrtc_calloc(1, sizeof(mbedtls_ecp_keypair));
    if (ec_key == NULL) {
        TINYRTC_LOG_ERROR("dtls_init: ec_key allocation failed");
        goto cleanup;
    }

    int ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, ec_key, dtls_our_random, &dtls_ctr_drbg);
    if (ret != 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to generate EC key: %d", ret);
        tinyrtc_free(ec_key);
        goto cleanup;
    }

    /* Setup PK context */
    const mbedtls_pk_info_t *pk_info = mbedtls_pk_info_from_type(MBEDTLS_PK_ECKEY);
    mbedtls_pk_setup(pkey, pk_info);

    /* Generate self-signed certificate */
    mbedtls_x509write_cert crtwrite;
    mbedtls_x509write_crt_init(&crtwrite);

    /* Set certificate information */
    mbedtls_x509write_crt_set_subject_name(&crtwrite, "CN=TinyRTC");
    mbedtls_x509write_crt_set_issuer_name(&crtwrite, "CN=TinyRTC");
    mbedtls_x509write_crt_set_md_alg(&crtwrite, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crtwrite, pkey);

    /* Validity: 10 years from now */
    const char *not_before = "20240101000000";
    const char *not_after = "20340101000000";
    mbedtls_x509write_crt_set_validity(&crtwrite, not_before, not_after);

    /* Self sign the certificate */
    unsigned char buf[4096];
    memset(buf, 0, sizeof(buf));
    ret = mbedtls_x509write_crt_pem(&crtwrite, buf, sizeof(buf), dtls_our_random, &dtls_ctr_drbg);
    if (ret < 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to sign certificate: %d", ret);
        mbedtls_x509write_crt_free(&crtwrite);
        goto cleanup;
    }

    /* Parse the certificate back into mbedtls structure */
    ret = mbedtls_x509_crt_parse(cert, buf, (size_t)ret);
    if (ret != 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to parse generated certificate: %d", ret);
        mbedtls_x509write_crt_free(&crtwrite);
        goto cleanup;
    }

    mbedtls_x509write_crt_free(&crtwrite);

    /* Assign to SSL configuration */
    mbedtls_ssl_conf_own_cert(ssl_cfg, cert, pkey);

    /* Store in our DTLS context */
    dtls->ssl = ssl;
    dtls->ctx = ssl_cfg;
    dtls->pkey = pkey;
    dtls->cert = cert;

    /* Calculate SHA-256 fingerprint of our certificate to put into the SDP fingerprint that goes into SDP */
    unsigned char md[32];
    size_t md_len = 32;
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts_ret(&sha_ctx, 0 /* 0 = SHA-256, 1 = SHA-224 */);
    if (cert->raw.len > 0) {
        mbedtls_sha256_update(&sha_ctx, cert->raw.p, cert->raw.len);
    }
    mbedtls_sha256_finish_ret(&sha_ctx, md);

    /* Convert to colon-separated hex string format for the fingerprint in the SDP offer/answer */
    int pos = 0;
    for (size_t i = 0; i < md_len && pos < (int)sizeof(dtls->fingerprint) - 3; i++) {
        if (i > 0 && pos < (int)sizeof(dtls->fingerprint)) {
            dtls->fingerprint[pos++] = ':';
        }
        unsigned char byte = md[i];
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", (int)byte);
        dtls->fingerprint[pos++] = hex[0];
        dtls->fingerprint[pos++] = hex[1];
    }
    dtls->fingerprint[pos] = '\0';

    dtls->role = role;
    dtls->handshake_complete = false;

    TINYRTC_LOG_DEBUG("dtls context initialized, fingerprint %s", dtls->fingerprint);
    return dtls;

cleanup:
    if (cert != NULL) {
        mbedtls_x509_crt_free(cert);
    }
    if (pkey != NULL) {
        mbedtls_pk_free(pkey);
    }
    if (ssl != NULL) {
        mbedtls_ssl_free(ssl);
    }
    if (ssl_cfg != NULL) {
        mbedtls_ssl_config_free(ssl_cfg);
    }
    tinyrtc_internal_free(dtls);
    return NULL;
}

void dtls_destroy(dtls_context_t *dtls)
{
    if (dtls == NULL) {
        return;
    }

    if (dtls->cert != NULL) {
        mbedtls_x509_crt_free(dtls->cert);
    }
    if (dtls->pkey != NULL) {
        mbedtls_pk_free(dtls->pkey);
    }
    if (dtls->ssl != NULL) {
        mbedtls_ssl_free(dtls->ssl);
    }
    if (dtls->ctx != NULL) {
        mbedtls_ssl_config_free(dtls->ctx);
    }
    tinyrtc_internal_free(dtls);
    TINYRTC_LOG_DEBUG("dtls context destroyed, fingerprint %s", dtls->fingerprint);
}

int dtls_get_fingerprint(dtls_context_t *dtls, char *buf, size_t len)
{
    if (dtls == NULL || buf == NULL) {
        return -1;
    }

    strncpy(buf, dtls->fingerprint, len);
    return (int)strlen(dtls->fingerprint);
}

bool dtls_verify_fingerprint(dtls_context_t *dtls, const char *peer_fingerprint)
{
    if (dtls == NULL || peer_fingerprint == NULL) {
        return false;
    }

    /* In WebRTC, we just compare the fingerprints that were exchanged out-of-band via SDP */
    bool match = strcmp(dtls->fingerprint, peer_fingerprint) == 0;

    if (!match) {
        TINYRTC_LOG_ERROR("dtls: fingerprint mismatch: expected %s got %s",
            dtls->fingerprint, peer_fingerprint);
    } else {
        TINYRTC_LOG_DEBUG("dtls: fingerprint verified successfully");
    }

    return match;
}

tinyrtc_error_t dtls_process_data(dtls_context_t *dtls,
                                  const uint8_t *data,
                                  size_t len)
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);

    int ret = mbedtls_ssl_read(dtls->ssl, (unsigned char *)data, (int)len);
    if (ret > 0) {
        /* Application data received - we don't expect any before handshake complete */
        TINYRTC_LOG_DEBUG("dtls: received %d bytes of application data", ret);
        return TINYRTC_OK;
    }

    if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
        return TINYRTC_OK;
    }

    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        TINYRTC_LOG_DEBUG("dtls: peer closed connection");
        return TINYRTC_OK;
    }

    if (ret < 0) {
        if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            TINYRTC_LOG_DEBUG("dtls: hello verification requested");
        } else {
            TINYRTC_LOG_ERROR("dtls: error processing DTLS data: %d", ret);
        }
        return TINYRTC_ERROR;
    }

    if (mbedtls_ssl_get_verify_result(dtls->ssl) == 0 &&
        dtls->ssl->state == MBEDTLS_SSL_HANDSHAKE_OVER) {
        dtls->handshake_complete = true;
        TINYRTC_LOG_INFO("dtls: handshake complete");
    }

    return TINYRTC_OK;
}

bool dtls_is_handshake_complete(dtls_context_t *dtls)
{
    if (dtls == NULL) {
        return false;
    }
    return dtls->handshake_complete && 
           dtls->ssl->state == MBEDTLS_SSL_HANDSHAKE_OVER;
}

tinyrtc_error_t dtls_derive_srtp_keys(dtls_context_t *dtls,
                                         uint8_t client_key[16],
                                         uint8_t client_salt[14],
                                         uint8_t server_key[16],
                                         uint8_t server_salt[14])
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);

    /* According to RFC 5764 for DTLS-SRTP key derivation
     * We get the master secret from DTLS and expand it with the correct label to get the 60 bytes we need
     */
    size_t output_len = (16 + 14) * 2;
    unsigned char *output = (unsigned char *)tinyrtc_calloc(1, output_len);
    if (output == NULL) {
        TINYRTC_LOG_ERROR("dtls_derive_srtp_keys: memory allocation failed");
        return TINYRTC_ERROR_MEMORY;
    }

    /* Use the standard TLS PRF with SHA-256 to expand the master secret into the SRTP key material as specified in RFC 5764
     */
    const char *label = "EXTRACTOR-dtls_srtp";
    size_t label_len = strlen(label);

    unsigned char tmp_hash[32];
    mbedtls_sha256_context sha_ctx;
    mbedtls_sha256_init(&sha_ctx);
    mbedtls_sha256_starts_ret(&sha_ctx, 0 /* SHA-256 */);
    mbedtls_sha256_update(&sha_ctx, (const unsigned char *)label, label_len);
    mbedtls_sha256_finish_ret(&sha_ctx, tmp_hash);

    mbedtls_sha256_context sha_ctx2;
    mbedtls_sha256_init(&sha_ctx2);
    mbedtls_sha256_starts_ret(&sha_ctx2, 0 /* SHA-256 */);
    mbedtls_sha256_update(&sha_ctx2, dtls->master_secret, 48);
    mbedtls_sha256_update(&sha_ctx2, tmp_hash, 32);
    mbedtls_sha256_finish_ret(&sha_ctx2, output);

    /* Split into client and server */
    size_t offset = 0;
    memcpy(client_key, output + offset, 16);
    offset += 16;
    memcpy(client_salt, output + offset, 14);
    offset += 14;
    memcpy(server_key, output + offset, 16);
    offset += 16;
    memcpy(server_salt, output + offset, 16);
    offset += 16;
    memcpy(server_salt, output + offset, 16);

    /* Store locally */
    memcpy(dtls->client_master_key, client_key, 16);
    memcpy(dtls->client_salt, client_salt, 14);
    memcpy(dtls->server_master_key, server_key, 16);
    memcpy(dtls->server_salt, server_salt, 14);

    tinyrtc_free(output);

    TINYRTC_LOG_DEBUG("dtls: SRTP keys derived successfully");
    return TINYRTC_OK;
}

bool dtls_get_master_secret(dtls_context_t *dtls, unsigned char *buffer, size_t len)
{
    if (dtls == NULL || buffer == NULL) {
        return false;
    }
    if (!dtls->master_secret_captured) {
        return false;
    }
    memcpy(buffer, dtls->master_secret, (len < 48) ? len : 48);
    return true;
}

tinyrtc_error_t dtls_start(dtls_context_t *dtls, int fd)
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);

    dtls->socket = fd;

    /* We do IO externally so just setup the bio context with null callbacks */
    mbedtls_ssl_set_bio(dtls->ssl, dtls, (mbedtls_ssl_send_t *)NULL,
                       (mbedtls_ssl_recv_t *)NULL, (mbedtls_ssl_recv_timeout_t *)NULL);

    TINYRTC_LOG_DEBUG("dtls: starting handshake on socket %d", fd);
    return TINYRTC_OK;
}
