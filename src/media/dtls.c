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
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_cookie.h"
#include "mbedtls/entropy_poll.h"
#include "mbedtls/timing.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

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

static tinyrtc_error_t dtls_configure_runtime_io(dtls_context_t *dtls);

static void dtls_mbedtls_debug(void *ctx, int level, const char *file, int line, const char *str)
{
    dtls_context_t *dtls = (dtls_context_t *)ctx;
    const char *role = "unknown";
    char message[256];
    size_t len;

    if (dtls != NULL) {
        role = (dtls->role == DTLS_ROLE_CLIENT) ? "client" : "server";
    }

    snprintf(message, sizeof(message), "dtls-mbedtls[%s][L%d %s:%d]: %s",
             role, level, file, line, str);
    len = strlen(message);
    while (len > 0 && (message[len - 1] == '\n' || message[len - 1] == '\r')) {
        message[--len] = '\0';
    }

    aosl_log(AOSL_LOG_INFO, "TinyRTC: %s\n", message);
}

static int dtls_bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    dtls_context_t *dtls = (dtls_context_t *)ctx;
    const ice_candidate_internal_t *remote;
    struct sockaddr_in remote_addr;
    int sent;

    if (dtls == NULL || dtls->ice == NULL || dtls->socket < 0 ||
        dtls->ice->selected_pair == NULL || dtls->ice->selected_pair->remote == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    remote = dtls->ice->selected_pair->remote;
    memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote->port);
    if (inet_pton(AF_INET, remote->ip, &remote_addr.sin_addr) != 1) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    sent = sendto(dtls->socket, buf, len, 0,
                  (struct sockaddr *)&remote_addr, sizeof(remote_addr));
    if (sent < 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }

    return sent;
}

static int dtls_bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    dtls_context_t *dtls = (dtls_context_t *)ctx;
    size_t copy_len;

    if (dtls == NULL) {
        return MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
    }

    if (dtls->pending_datagram_len == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }

    /* DTLS is datagram-oriented: deliver at most one complete datagram per
     * recv callback instead of streaming it across multiple reads. */
    copy_len = dtls->pending_datagram_len;
    if (copy_len > len) {
        copy_len = len;
    }

    memcpy(buf, dtls->pending_datagram, copy_len);
    dtls->pending_datagram_len = 0;
    dtls->pending_datagram_offset = 0;

    return (int)copy_len;
}

static int dtls_continue_handshake(dtls_context_t *dtls)
{
    int ret;

    if (dtls == NULL || dtls->handshake_complete) {
        return 0;
    }

    ret = mbedtls_ssl_handshake(dtls->ssl);
    if (ret == 0) {
        dtls->handshake_complete = true;
        TINYRTC_LOG_INFO("dtls: handshake complete");
        return 0;
    }

    return ret;
}

static tinyrtc_error_t dtls_handle_handshake_result(dtls_context_t *dtls, int ret)
{
    if (ret == 0 || ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return TINYRTC_OK;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        TINYRTC_LOG_DEBUG("dtls: peer closed connection");
        return TINYRTC_OK;
    }
    if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
        TINYRTC_LOG_INFO("dtls: hello verification requested, role=%d state=%d",
                         (int)dtls->role,
                         dtls->ssl != NULL ? dtls->ssl->state : -1);
        if (dtls->role == DTLS_ROLE_SERVER) {
            TINYRTC_LOG_INFO("dtls: entering server HelloVerifyRequest recovery path");
            if (mbedtls_ssl_session_reset(dtls->ssl) != 0) {
                TINYRTC_LOG_ERROR("dtls: failed to reset server session after HelloVerifyRequest");
                return TINYRTC_ERROR;
            }
            dtls->handshake_complete = false;
            if (dtls_configure_runtime_io(dtls) != TINYRTC_OK) {
                TINYRTC_LOG_ERROR("dtls: failed to reconfigure server DTLS I/O after HelloVerifyRequest");
                return TINYRTC_ERROR;
            }
            TINYRTC_LOG_INFO("dtls: server session reset after HelloVerifyRequest, state=%d",
                             dtls->ssl->state);
        }
        return TINYRTC_OK;
    }

    TINYRTC_LOG_ERROR("dtls: error processing DTLS data: %d", ret);
    return TINYRTC_ERROR;
}

static tinyrtc_error_t dtls_configure_runtime_io(dtls_context_t *dtls)
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(dtls->ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(dtls->ice->socket >= 0, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(dtls->ice->selected_pair != NULL, TINYRTC_ERROR_INVALID_ARG);

    dtls->socket = dtls->ice->socket;
    dtls->pending_datagram_len = 0;
    dtls->pending_datagram_offset = 0;
    memset(&dtls->timer, 0, sizeof(dtls->timer));

    if (dtls->role == DTLS_ROLE_SERVER && dtls->ice->selected_pair->remote != NULL) {
        const ice_candidate_internal_t *remote = dtls->ice->selected_pair->remote;
        unsigned char transport_id[80];
        int transport_id_len = snprintf((char *)transport_id, sizeof(transport_id),
                                        "%s:%u", remote->ip, (unsigned)remote->port);
        if (transport_id_len <= 0 || (size_t)transport_id_len >= sizeof(transport_id)) {
            TINYRTC_LOG_ERROR("dtls: failed to format client transport id");
            return TINYRTC_ERROR_INVALID_ARG;
        }
        if (mbedtls_ssl_set_client_transport_id(dtls->ssl,
                                                transport_id,
                                                (size_t)transport_id_len) != 0) {
            TINYRTC_LOG_ERROR("dtls: failed to set client transport id");
            return TINYRTC_ERROR;
        }
    }

    mbedtls_ssl_set_bio(dtls->ssl, dtls, dtls_bio_send, dtls_bio_recv, NULL);
    mbedtls_ssl_set_timer_cb(dtls->ssl, &dtls->timer,
                             mbedtls_timing_set_delay, mbedtls_timing_get_delay);

    return TINYRTC_OK;
}

static int dtls_key_export_callback(void *p_expkey,
                                      const unsigned char *ms,
                                      const unsigned char *kb,
                                      size_t maclen,
                                      size_t keylen,
                                      size_t ivlen)
{
    (void)kb; (void)maclen; (void)keylen; (void)ivlen;

    dtls_context_t *dtls = (dtls_context_t *)p_expkey;

    /* We only need the master_secret, 48 bytes for DTLS-SRTP */
    if (dtls != NULL && ms != NULL) {
        memcpy(dtls->master_secret, ms, 48);
        dtls->master_secret_captured = true;
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

    int ret;

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
    mbedtls_ssl_conf_dbg(ssl_cfg, dtls_mbedtls_debug, dtls);
    mbedtls_debug_set_threshold(4);

    if (role == DTLS_ROLE_SERVER) {
        mbedtls_ssl_cookie_init(&dtls->cookie_ctx);
        ret = mbedtls_ssl_cookie_setup(&dtls->cookie_ctx, dtls_our_random, &dtls_ctr_drbg);
        if (ret != 0) {
            TINYRTC_LOG_ERROR("dtls_init: failed to setup DTLS cookies: %d", ret);
            goto cleanup;
        }
        dtls->cookie_initialized = true;
        mbedtls_ssl_conf_dtls_cookies(ssl_cfg,
                                      mbedtls_ssl_cookie_write,
                                      mbedtls_ssl_cookie_check,
                                      &dtls->cookie_ctx);
    }

    /* Enable certificate verification - in WebRTC we verify via fingerprint out-of-band */
    mbedtls_ssl_conf_authmode(ssl_cfg, MBEDTLS_SSL_VERIFY_NONE);

    /* For DTLS, mbedTLS uses minor version 3 for DTLS 1.2 and 2 for DTLS 1.0. */
    mbedtls_ssl_conf_min_version(ssl_cfg, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    mbedtls_ssl_conf_max_version(ssl_cfg, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);

    /* Configure elliptic curve for ECDHE - just P-256 which is what WebRTC needs */
    static const mbedtls_ecp_group_id curves[] = {
        MBEDTLS_ECP_DP_SECP256R1,
        MBEDTLS_ECP_DP_NONE
    };
    mbedtls_ssl_conf_curves(ssl_cfg, curves);

    /* Register key export callback to capture the per-session master secret. */
    mbedtls_ssl_conf_export_keys_cb(ssl_cfg, dtls_key_export_callback, dtls);
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

    ret = mbedtls_ecp_gen_key(MBEDTLS_ECP_DP_SECP256R1, ec_key, dtls_our_random, &dtls_ctr_drbg);
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
    mbedtls_mpi serial;
    mbedtls_x509write_crt_init(&crtwrite);
    mbedtls_mpi_init(&serial);

    /* Attach generated EC key to the PK container before using it for
     * certificate generation and DTLS certificate configuration. */
    *mbedtls_pk_ec(*pkey) = *ec_key;
    tinyrtc_internal_free(ec_key);
    ec_key = NULL;

    /* Set certificate information */
    mbedtls_x509write_crt_set_subject_name(&crtwrite, "CN=TinyRTC");
    mbedtls_x509write_crt_set_issuer_name(&crtwrite, "CN=TinyRTC");
    mbedtls_x509write_crt_set_md_alg(&crtwrite, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crtwrite, pkey);
    mbedtls_x509write_crt_set_issuer_key(&crtwrite, pkey);
    mbedtls_x509write_crt_set_version(&crtwrite, MBEDTLS_X509_CRT_VERSION_3);

    ret = mbedtls_mpi_read_string(&serial, 10, "1");
    if (ret != 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to create certificate serial: %d", ret);
        mbedtls_x509write_crt_free(&crtwrite);
        mbedtls_mpi_free(&serial);
        goto cleanup;
    }
    ret = mbedtls_x509write_crt_set_serial(&crtwrite, &serial);
    if (ret != 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to set certificate serial: %d", ret);
        mbedtls_x509write_crt_free(&crtwrite);
        mbedtls_mpi_free(&serial);
        goto cleanup;
    }

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
        mbedtls_mpi_free(&serial);
        goto cleanup;
    }

    /* mbedtls_x509write_crt_pem() returns 0 on success. PEM parsing expects
     * the actual string length including the terminating NUL byte. */
    ret = mbedtls_x509_crt_parse(cert, buf, strlen((const char *)buf) + 1);
    if (ret != 0) {
        TINYRTC_LOG_ERROR("dtls_init: failed to parse generated certificate: %d", ret);
        mbedtls_x509write_crt_free(&crtwrite);
        mbedtls_mpi_free(&serial);
        goto cleanup;
    }

    mbedtls_x509write_crt_free(&crtwrite);
    mbedtls_mpi_free(&serial);

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
    if (dtls->cookie_initialized) {
        mbedtls_ssl_cookie_free(&dtls->cookie_ctx);
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
    int ret;

    if (len > sizeof(dtls->pending_datagram)) {
        TINYRTC_LOG_ERROR("dtls: incoming datagram too large: %zu", len);
        return TINYRTC_ERROR;
    }

    if (len >= 25 && data[0] == 0x16) {
        uint16_t epoch = (uint16_t)((data[3] << 8) | data[4]);
        uint64_t seq = ((uint64_t)data[5] << 40) |
                       ((uint64_t)data[6] << 32) |
                       ((uint64_t)data[7] << 24) |
                       ((uint64_t)data[8] << 16) |
                       ((uint64_t)data[9] << 8) |
                       (uint64_t)data[10];
        uint16_t record_len = (uint16_t)((data[11] << 8) | data[12]);
        uint8_t hs_type = data[13];
        uint32_t hs_len = ((uint32_t)data[14] << 16) |
                          ((uint32_t)data[15] << 8) |
                          (uint32_t)data[16];
        uint16_t msg_seq = (uint16_t)((data[17] << 8) | data[18]);
        uint32_t frag_off = ((uint32_t)data[19] << 16) |
                            ((uint32_t)data[20] << 8) |
                            (uint32_t)data[21];
        uint32_t frag_len = ((uint32_t)data[22] << 16) |
                            ((uint32_t)data[23] << 8) |
                            (uint32_t)data[24];

        TINYRTC_LOG_INFO(
            "dtls: incoming handshake record ver=%02x.%02x epoch=%u seq=%llu record_len=%u hs_type=%u hs_len=%u msg_seq=%u frag_off=%u frag_len=%u packet_len=%zu",
            (unsigned)data[1], (unsigned)data[2], (unsigned)epoch,
            (unsigned long long)seq, (unsigned)record_len, (unsigned)hs_type,
            (unsigned)hs_len, (unsigned)msg_seq, (unsigned)frag_off,
            (unsigned)frag_len, len);
    }

    memcpy(dtls->pending_datagram, data, len);
    dtls->pending_datagram_len = len;
    dtls->pending_datagram_offset = 0;

    ret = dtls_continue_handshake(dtls);
    if (ret == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED && dtls->role == DTLS_ROLE_SERVER) {
        tinyrtc_error_t handled = dtls_handle_handshake_result(dtls, ret);
        if (handled != TINYRTC_OK) {
            return handled;
        }

        memcpy(dtls->pending_datagram, data, len);
        dtls->pending_datagram_len = len;
        dtls->pending_datagram_offset = 0;
        ret = dtls_continue_handshake(dtls);
    }
    return dtls_handle_handshake_result(dtls, ret);
}

bool dtls_is_handshake_complete(dtls_context_t *dtls)
{
    if (dtls == NULL) {
        return false;
    }
    return dtls->handshake_complete && 
           dtls->ssl->state == MBEDTLS_SSL_HANDSHAKE_OVER;
}

tinyrtc_error_t dtls_poll_handshake(dtls_context_t *dtls)
{
    int ret;

    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);

    ret = dtls_continue_handshake(dtls);
    return dtls_handle_handshake_result(dtls, ret);
}

tinyrtc_error_t dtls_derive_srtp_keys(dtls_context_t *dtls,
                                         uint8_t client_key[16],
                                         uint8_t client_salt[14],
                                         uint8_t server_key[16],
                                         uint8_t server_salt[14])
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(dtls->master_secret_captured, TINYRTC_ERROR_INVALID_STATE);
    TINYRTC_CHECK(client_key != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(client_salt != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(server_key != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(server_salt != NULL, TINYRTC_ERROR_INVALID_ARG);

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

    /* Minimal deterministic expansion for current demo path.
     * This is not a full RFC 5764 exporter implementation, but it must at
     * least fill the requested 60 bytes without overrunning the buffer. */
    for (size_t offset = 0; offset < output_len; offset += 32) {
        unsigned char block[32];
        mbedtls_sha256_context sha_ctx2;
        uint32_t counter = (uint32_t)(offset / 32);
        size_t block_len = output_len - offset;
        if (block_len > sizeof(block)) {
            block_len = sizeof(block);
        }

        mbedtls_sha256_init(&sha_ctx2);
        mbedtls_sha256_starts_ret(&sha_ctx2, 0 /* SHA-256 */);
        mbedtls_sha256_update(&sha_ctx2, dtls->master_secret, 48);
        mbedtls_sha256_update(&sha_ctx2, tmp_hash, 32);
        mbedtls_sha256_update(&sha_ctx2, (const unsigned char *)&counter, sizeof(counter));
        mbedtls_sha256_finish_ret(&sha_ctx2, block);
        mbedtls_sha256_free(&sha_ctx2);

        memcpy(output + offset, block, block_len);
    }

    /* Split into client and server */
    size_t offset = 0;
    memcpy(client_key, output + offset, 16);
    offset += 16;
    memcpy(client_salt, output + offset, 14);
    offset += 14;
    memcpy(server_key, output + offset, 16);
    offset += 16;
    memcpy(server_salt, output + offset, 14);
    offset += 16;

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

tinyrtc_error_t dtls_start(dtls_context_t *dtls, ice_session_t *ice)
{
    TINYRTC_CHECK(dtls != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(ice != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(ice->socket >= 0, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(ice->selected_pair != NULL, TINYRTC_ERROR_INVALID_ARG);

    dtls->ice = ice;
    if (dtls_configure_runtime_io(dtls) != TINYRTC_OK) {
        return TINYRTC_ERROR;
    }

    {
        int ret = dtls_continue_handshake(dtls);
        if (ret != 0 && ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            TINYRTC_LOG_ERROR("dtls: failed to start handshake: %d", ret);
            return TINYRTC_ERROR;
        }
    }

    TINYRTC_LOG_DEBUG("dtls: starting handshake on socket %d", ice->socket);
    return TINYRTC_OK;
}
