/**
 * @file signaling.c
 * @brief Signaling client implementation for WebSocket-based SDP exchange
 */

#include "tinyrtc/signaling.h"
#include "tinyrtc/tinyrtc.h"
#include "common.h"
#include "api/aosl.h"

/* Forward declaration */
struct tinyrtc_context;

#include <tinyrtc/tinyrtc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>

#include "mbedtls/net.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha1.h"

/* WebSocket opcodes */
#define WS_OPCODE_CONTINUE   0x00
#define WS_OPCODE_TEXT       0x01
#define WS_OPCODE_BINARY     0x02
#define WS_OPCODE_CLOSE      0x08
#define WS_OPCODE_PING       0x09
#define WS_OPCODE_PONG       0x0A

/* Buffer size limits */
#define SIG_MAX_RECV_BUF     8192
#define SIG_MAX_SEND_BUF     8192

static bool sig_ssl_configured(struct tinyrtc_signaling *sig);

struct tinyrtc_signaling {
    /* TinyRTC context back reference */
    tinyrtc_context_t *tinyrtc_ctx;

    /* Configuration */
    char server_url[256];
    char room_id[128];
    char client_id[64];
    bool auto_connect;
    bool is_wss;             /* Whether we're using SSL/TLS (wss) */
    bool disable_cert_verify; /* Disable SSL certificate verification */

    /* State */
    tinyrtc_signaling_state_t state;
    char last_error[256];

    /* mbedTLS context */
    mbedtls_net_context net;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;

    /* Receive buffer */
    uint8_t recv_buf[SIG_MAX_RECV_BUF];
    size_t recv_len;

    /* Buffering for fragmented WebSocket messages */
    uint8_t *message_buf;
    size_t message_len;
    size_t message_cap;

    /* User callback */
    tinyrtc_signal_callback_t callback;
    void *user_data;
};

/* Check if SSL is configured - we stored this flag when creating the connection */
static bool sig_ssl_configured(struct tinyrtc_signaling *sig) {
    return sig->is_wss;
}

/* Forward declarations */
static int sig_parse_url(const char *url, char *host, size_t host_len,
                          uint16_t *port, bool *is_wss);
static int sig_perform_websocket_handshake(struct tinyrtc_signaling *sig,
                                            const char *host, const char *path);
static int sig_read_ws_frame(struct tinyrtc_signaling *sig);
static int sig_send_ws_frame(struct tinyrtc_signaling *sig, uint8_t opcode,
                              const uint8_t *data, size_t len);
static int sig_configure_tls_client(struct tinyrtc_signaling *sig,
                                    bool ca_loaded,
                                    bool verify_disabled);
static void sig_process_message(struct tinyrtc_signaling *sig, const uint8_t *data, size_t len);
static char *sig_extract_json_string_field(const char *json, const char *field_name);
static char *sig_extract_sdp_value(const char *json);
static tinyrtc_ice_candidate_t *sig_extract_candidate_value(const char *json);
static void sig_free_candidate(tinyrtc_ice_candidate_t *candidate);

/* Generate a random client ID */
static void sig_generate_client_id(struct tinyrtc_signaling *sig, char *buf, size_t len) {
    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (size_t i = 0; i < len - 1; i++) {
        unsigned char rnd;
        mbedtls_ctr_drbg_random(&sig->ctr_drbg, &rnd, 1);
        buf[i] = charset[rnd % (sizeof(charset) - 1)];
    }
    buf[len - 1] = '\0';
}

static char *sig_extract_json_string_field(const char *json, const char *field_name)
{
    char pattern[64];
    const char *found;
    const char *value;
    const char *end;
    size_t raw_len;
    char *out;
    size_t i, j;

    snprintf(pattern, sizeof(pattern), "\"%s\"", field_name);
    found = strstr(json, pattern);
    if (found == NULL) {
        return NULL;
    }

    value = strchr(found + strlen(pattern), ':');
    if (value == NULL) {
        return NULL;
    }
    value++;
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (*value != '"') {
        return NULL;
    }
    value++;

    end = value;
    while (*end != '\0') {
        if (*end == '"' && (end == value || *(end - 1) != '\\')) {
            break;
        }
        end++;
    }
    if (*end != '"') {
        return NULL;
    }

    raw_len = (size_t)(end - value);
    out = (char *)aosl_malloc(raw_len + 1);
    if (out == NULL) {
        return NULL;
    }

    i = 0;
    j = 0;
    while (i < raw_len && value[i] != '\0') {
        if (value[i] == '\\' && i + 1 < raw_len) {
            if (value[i + 1] == 'r') {
                out[j++] = '\r';
                i += 2;
            } else if (value[i + 1] == 'n') {
                out[j++] = '\n';
                i += 2;
            } else if (value[i + 1] == 't') {
                out[j++] = '\t';
                i += 2;
            } else if (value[i + 1] == '"' || value[i + 1] == '\\' || value[i + 1] == '/') {
                out[j++] = value[i + 1];
                i += 2;
            } else {
                out[j++] = value[i++];
            }
        } else {
            out[j++] = value[i++];
        }
    }
    out[j] = '\0';

    return out;
}

static char *sig_extract_sdp_value(const char *json)
{
    char *found;
    char *value;

    found = strstr(json, "\"sdp\"");
    if (found == NULL) {
        return NULL;
    }

    value = strchr(found + 5, ':');
    if (value == NULL) {
        return NULL;
    }
    value++;
    while (*value == ' ' || *value == '\t') {
        value++;
    }

    if (*value == '"') {
        return sig_extract_json_string_field(found, "sdp");
    }

    if (*value == '{') {
        return sig_extract_json_string_field(value, "sdp");
    }

    return NULL;
}

static tinyrtc_ice_candidate_t *sig_extract_candidate_value(const char *json)
{
    char *found;
    char *value;
    char *candidate_str = NULL;
    tinyrtc_ice_candidate_t *candidate = NULL;
    char foundation[32] = {0};
    char protocol[32] = {0};
    char ip[128] = {0};
    char type[32] = {0};
    char prio_str[32] = {0};
    char port_str[16] = {0};
    int port = 0;
    unsigned long priority = 0;
    char *cursor;

    found = strstr(json, "\"candidate\"");
    if (found == NULL) {
        return NULL;
    }

    value = strchr(found + 11, ':');
    if (value == NULL) {
        return NULL;
    }
    value++;
    while (*value == ' ' || *value == '\t') {
        value++;
    }

    if (*value == '"') {
        candidate_str = sig_extract_json_string_field(found, "candidate");
    } else if (*value == '{') {
        candidate_str = sig_extract_json_string_field(value, "candidate");
    }

    if (candidate_str == NULL) {
        return NULL;
    }

    cursor = candidate_str;
    if (strncmp(cursor, "candidate:", 10) == 0) {
        cursor += 10;
    }

    if (sscanf(cursor, "%31s %*d %31s %31s %127s %15s typ %31s",
               foundation, protocol, prio_str, ip, port_str, type) != 6) {
        aosl_free(candidate_str);
        return NULL;
    }

    priority = strtoul(prio_str, NULL, 10);
    port = atoi(port_str);
    if (port <= 0) {
        aosl_free(candidate_str);
        return NULL;
    }

    candidate = (tinyrtc_ice_candidate_t *)aosl_calloc(1, sizeof(*candidate));
    if (candidate == NULL) {
        aosl_free(candidate_str);
        return NULL;
    }

    candidate->foundation = tinyrtc_strdup(foundation);
    candidate->protocol = tinyrtc_strdup(protocol);
    candidate->ip = tinyrtc_strdup(ip);
    candidate->type = tinyrtc_strdup(type);
    candidate->priority = (uint32_t)priority;
    candidate->port = (uint16_t)port;
    candidate->is_ipv6 = strchr(ip, ':') != NULL;

    aosl_free(candidate_str);

    if (candidate->foundation == NULL || candidate->protocol == NULL ||
        candidate->ip == NULL || candidate->type == NULL) {
        sig_free_candidate(candidate);
        return NULL;
    }

    return candidate;
}

static void sig_free_candidate(tinyrtc_ice_candidate_t *candidate)
{
    if (candidate == NULL) {
        return;
    }
    tinyrtc_internal_free(candidate->foundation);
    tinyrtc_internal_free(candidate->ip);
    tinyrtc_internal_free(candidate->type);
    tinyrtc_internal_free(candidate->protocol);
    tinyrtc_internal_free(candidate);
}

/* Parse URL into host, port, and whether it's secure (wss) */
static int sig_parse_url(const char *url, char *host, size_t host_len,
                          uint16_t *port, bool *is_wss) {
    *is_wss = false;
    *port = 80;

    if (strncmp(url, "ws://", 5) == 0) {
        url += 5;
        *port = 80;
    } else if (strncmp(url, "wss://", 6) == 0) {
        url += 6;
        *port = 443;
        *is_wss = true;
    } else {
        return -1;
    }

    /* Find next slash or colon */
    const char *slash = strchr(url, '/');
    const char *colon = strchr(url, ':');

    if (colon && (!slash || colon < slash)) {
        /* Has port */
        size_t host_len_actual = colon - url;
        if (host_len_actual >= host_len) {
            return -1;
        }
        memcpy(host, url, host_len_actual);
        host[host_len_actual] = '\0';
        colon++;
        *port = (uint16_t)atoi(colon);
    } else {
        /* No port */
        size_t len = slash ? (size_t)(slash - url) : strlen(url);
        if (len >= host_len) {
            return -1;
        }
        memcpy(host, url, len);
        host[len] = '\0';
    }

    return 0;
}

/* Standard Base64 encoding - correctly handles any input length */
static const char *base64_chars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const unsigned char *input, size_t input_len, char *output, size_t output_len)
{
    size_t output_size = ((input_len + 2) / 3) * 4;
    // Check output buffer size
    if (output_len < output_size + 1) {
        return 0; // buffer too small
    }

    size_t i = 0;
    size_t j = 0;
    unsigned char buf[3];
    unsigned int n = 0;

    while (i < input_len) {
        buf[n++] = input[i++];
        if (n == 3) {
            // we have 3 bytes, encode 4 6-bit chars
            output[j++] = base64_chars[(buf[0] >> 2) & 0x3F];
            output[j++] = base64_chars[((buf[0] & 0x03) << 4) | ((buf[1] >> 4) & 0x0F)];
            output[j++] = base64_chars[((buf[1] & 0x0F) << 2) | ((buf[2] >> 6) & 0x03)];
            output[j++] = base64_chars[buf[2] & 0x3F];
            n = 0;
        }
    }

    // handle remaining bytes (if any)
    if (n > 0) {
        // zero remaining bytes in buffer
        for (size_t k = n; k < 3; k++) {
            buf[k] = 0;
        }

        output[j++] = base64_chars[(buf[0] >> 2) & 0x3F];
        output[j++] = base64_chars[((buf[0] & 0x03) << 4) | ((buf[1] >> 4) & 0x0F)];
        if (n == 1) {
            output[j++] = '=';
            output[j++] = '=';
        } else if (n == 2) {
            output[j++] = base64_chars[((buf[1] & 0x0F) << 2) | ((buf[2] >> 6) & 0x03)];
            output[j++] = '=';
        }
    }

    output[j] = '\0';
    return output_size;
}

/* Generate WebSocket accept key according to RFC6455 */
static void sig_compute_accept_key(const char *client_key, char *accept, size_t accept_len) {
    /* The GUID from RFC6455 */
    static const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    size_t client_key_len = strlen(client_key);
    size_t concat_len = client_key_len + strlen(guid);
    unsigned char concat[512]; // client-key is at most 24 chars, guid is 36 → total 60 < 512
    memcpy(concat, client_key, client_key_len);
    memcpy(concat + client_key_len, guid, strlen(guid));

    /* SHA-1 hash using mbedTLS */
    unsigned char hash[20];
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, concat, concat_len);
    mbedtls_sha1_finish(&ctx, hash);
    mbedtls_sha1_free(&ctx);

    /* Base64 encode - using standard base64 encoding
     * SHA-1 always outputs exactly 20 bytes (160 bits) → 27 base64 chars + 1 padding = 28 total
     * accept buffer is at least 32 bytes, which is enough for 28 chars + null terminator
     */
    base64_encode(hash, 20, accept, accept_len);
    TINYRTC_LOG_DEBUG("sig_compute_accept_key: result '%s' len=%zu", accept, strlen(accept));
}

static int sig_perform_websocket_handshake(struct tinyrtc_signaling *sig,
                                            const char *host, const char *path) {
    /* Generate a random Sec-WebSocket-Key */
    unsigned char key_bytes[16];
    for (int i = 0; i < 16; i++) {
        mbedtls_ctr_drbg_random(&sig->ctr_drbg, &key_bytes[i], 1);
    }

    /* Base64 encode 16 bytes of random data -> exactly 24 base64 characters */
    char client_key[64]; // large enough buffer
    size_t encoded_len = base64_encode(key_bytes, 16, client_key, sizeof(client_key));
    (void)encoded_len;
    aosl_log(AOSL_LOG_DEBUG, "Signaling: generated client_key '%s' len=%zu", client_key, strlen(client_key));

    /* Build handshake request */
    char request[2048];
    int len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Origin: http://%s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, host, client_key
    );

    aosl_log(AOSL_LOG_DEBUG, "Signaling: sending WebSocket handshake");

    /* Send handshake */
    int ret;
    bool is_wss = sig_ssl_configured(sig);
    if (is_wss) {
        ret = mbedtls_ssl_write(&sig->ssl, (const unsigned char *)request, len);
    } else {
        ret = mbedtls_net_send(&sig->net, (const unsigned char *)request, len);
    }

    if (ret < 0) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Failed to send handshake: %d", ret);
        return -1;
    }

    /* Read response */
    char response[2048];
    memset(response, 0, sizeof(response));

    int total = 0;
    int max_read = (int)sizeof(response) - 1;
    while (total < max_read) {
        unsigned char c;
        int r;
        if (is_wss) {
            r = mbedtls_ssl_read(&sig->ssl, &c, 1);
        } else {
            r = mbedtls_net_recv(&sig->net, &c, 1);
        }

        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            aosl_msleep(10);
            continue;
        }

        if (r <= 0) {
            break;
        }

        response[total++] = (char)c;

        /* Check for end of headers */
        if (total >= 4 &&
            response[total - 4] == '\r' &&
            response[total - 3] == '\n' &&
            response[total - 2] == '\r' &&
            response[total - 1] == '\n') {
            break;
        }
    }

    response[total] = '\0';

    /* Check response status code */
    if (strstr(response, "101 Switching Protocols") == NULL) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Bad handshake response: expected 101, got:\n%.100s", response);
        return -1;
    }

    /* Check Sec-WebSocket-Accept */
    char expected_accept[32]; // SHA-1 always produces exactly 28 bytes base64 + 1 null terminator, extra space to avoid overflow
    sig_compute_accept_key(client_key, expected_accept, sizeof(expected_accept));
    char *accept_header = strstr(response, "Sec-WebSocket-Accept:");
    if (!accept_header) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "No Sec-WebSocket-Accept in response");
        return -1;
    }

    accept_header += strlen("Sec-WebSocket-Accept:");
    while (*accept_header == ' ' || *accept_header == '\t') {
        accept_header++;
    }

    // The accept key is exactly 28 characters (28-base64 + null terminator = 29)
    // It ends at \r\n or end-of-headers
    char *accept_end = strchr(accept_header, '\r');
    if (accept_end) {
        *accept_end = '\0';
    } else {
        // If no \r found, search for \n instead
        accept_end = strchr(accept_header, '\n');
        if (accept_end) {
            *accept_end = '\0';
        }
        // If still not found, leave as-is (buffer is null terminated already)
    }

    if (strcmp(accept_header, expected_accept) != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Invalid Sec-WebSocket-Accept: expected %s (len=%zu), got %s (len=%zu)",
                 expected_accept, strlen(expected_accept), accept_header, strlen(accept_header));
        return -1;
    }

    aosl_log(AOSL_LOG_INFO, "Signaling: WebSocket handshake completed successfully\n");
    return 0;
}

static int sig_read_ws_frame(struct tinyrtc_signaling *sig) {
    /* Read header */
    uint8_t hdr[2];
    int ret;

    bool is_wss = sig_ssl_configured(sig);

    for (int i = 0; i < 2; i++) {
        if (is_wss) {
            ret = mbedtls_ssl_read(&sig->ssl, &hdr[i], 1);
        } else {
            ret = mbedtls_net_recv(&sig->net, &hdr[i], 1);
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            /* No data available right now, return immediately to allow main loop
             * continue processing ICE connectivity checks, send STUN pings, etc.
             * Sleeping here would just cause unnecessary delay and blocking. */
            /* No data available, not an error for polling */
            return 0;
        }

        if (ret <= 0) {
            /* Error/EOF */
            return -1;
        }
    }

    bool fin = (hdr[0] & 0x80) != 0;
    uint8_t opcode = hdr[0] & 0x0F;
    bool masked = (hdr[1] & 0x80) != 0;
    uint8_t payload_len = hdr[1] & 0x7F;

    size_t len = payload_len;
    int bytes_needed = 0;

    if (payload_len == 126) {
        bytes_needed = 2;
    } else if (payload_len == 127) {
        bytes_needed = 8;
    }

    /* Read extended length */
    if (bytes_needed > 0) {
        uint8_t ext_len[8];
        for (int i = 0; i < bytes_needed; i++) {
            if (is_wss) {
                ret = mbedtls_ssl_read(&sig->ssl, &ext_len[i], 1);
            } else {
                ret = mbedtls_net_recv(&sig->net, &ext_len[i], 1);
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                /* No data available right now, return immediately */
                return 0;
            }

            if (ret <= 0) {
                snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                         "Failed to read extended payload length");
                return -1;
            }
        }

        len = 0;
        if (bytes_needed == 2) {
            len = (ext_len[0] << 8) | ext_len[1];
        } else {
            for (int i = 0; i < 8; i++) {
                len = (len << 8) | ext_len[i];
            }
        }
    }

    /* Read masking key (server -> client should not be masked per RFC6455) */
    uint8_t mask_key[4];
    if (masked) {
        for (int i = 0; i < 4; i++) {
            if (is_wss) {
                ret = mbedtls_ssl_read(&sig->ssl, &mask_key[i], 1);
            } else {
                ret = mbedtls_net_recv(&sig->net, &mask_key[i], 1);
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                /* No data available right now, return immediately */
                return 0;
            }

            if (ret <= 0) {
                snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                         "Failed to read mask key");
                return -1;
            }
        }
    }

    /* Ensure we have buffer space */
    if (len > SIG_MAX_RECV_BUF) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "WebSocket frame too large: %zu bytes (max %d)",
                 len, SIG_MAX_RECV_BUF);
        return -1;
    }

    /* Read payload */
    for (size_t i = 0; i < len; i++) {
        if (is_wss) {
            ret = mbedtls_ssl_read(&sig->ssl, &sig->recv_buf[sig->recv_len + i], 1);
        } else {
            ret = mbedtls_net_recv(&sig->net, &sig->recv_buf[sig->recv_len + i], 1);
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            /* No data available right now, return immediately */
            return 0;
        }

        if (ret <= 0) {
            snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                     "Failed to read payload at offset %zu", i);
            return -1;
        }
    }
    sig->recv_len += len;

    /* Unmask if needed */
    if (masked) {
        for (size_t i = 0; i < len; i++) {
            sig->recv_buf[sig->recv_len - len + i] ^= mask_key[i % 4];
        }
    }

    /* Handle different opcodes */
    switch (opcode) {
        case WS_OPCODE_TEXT:
        case WS_OPCODE_BINARY:
            if (!fin) {
                /* Fragmented message, start/continue buffer */
                if (sig->message_buf == NULL) {
                    sig->message_cap = SIG_MAX_RECV_BUF;
                    sig->message_buf = (uint8_t *)aosl_malloc(sig->message_cap);
                    if (!sig->message_buf) {
                        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                                 "Out of memory for fragmented message");
                        return -1;
                    }
                    sig->message_len = 0;
                }

                /* Append to buffer */
                if (sig->message_len + len > sig->message_cap) {
                    snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                             "Fragmented message exceeds buffer size");
                    return -1;
                }
                memcpy(sig->message_buf + sig->message_len, sig->recv_buf, len);
                sig->message_len += len;
                sig->recv_len = 0;
                return 0; /* Need more fragments */
            }
            /* Finished single frame message */
            sig_process_message(sig, sig->recv_buf, len);
            sig->recv_len = 0;
            break;

        case WS_OPCODE_CONTINUE: {
            if (sig->message_buf == NULL) {
                snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                         "Invalid state: fragmented message without buffer");
                return -1;
            }
            if (sig->message_len + len > sig->message_cap) {
                snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                         "Fragmented message exceeds buffer size");
                return -1;
            }
            memcpy(sig->message_buf + sig->message_len, sig->recv_buf, len);
            sig->message_len += len;
            sig->recv_len = 0;

            if (fin) {
                /* Complete message */
                sig_process_message(sig, sig->message_buf, sig->message_len);
                aosl_free(sig->message_buf);
                sig->message_buf = NULL;
                sig->message_len = 0;
                sig->message_cap = 0;
            }
            break;
        }

        case WS_OPCODE_CLOSE: {
            aosl_log(AOSL_LOG_INFO, "Signaling: received close frame from server\n");
            sig->state = TINYRTC_SIGNALING_DISCONNECTED;
            return -1;
        }

        case WS_OPCODE_PING:
            /* Send pong */
            sig_send_ws_frame(sig, WS_OPCODE_PONG, sig->recv_buf, len);
            sig->recv_len = 0;
            break;

        case WS_OPCODE_PONG:
            /* Ignore pong */
            sig->recv_len = 0;
            break;

        default:
            aosl_log(AOSL_LOG_WARNING, "Signaling: unknown WebSocket opcode 0x%02x\n", opcode);
            sig->recv_len = 0;
            break;
    }

    return 0;
}

static int sig_send_ws_frame(struct tinyrtc_signaling *sig, uint8_t opcode,
                              const uint8_t *data, size_t len) {
    if (sig->state != TINYRTC_SIGNALING_CONNECTED) {
        return -1;
    }

    uint8_t header[16];
    size_t header_len = 2;

    header[0] = 0x80 | opcode; /* FIN bit set + opcode */

    if (len <= 125) {
        header[1] = (uint8_t)len;
        /* Client -> server must be masked */
        header[1] |= 0x80;
    } else if (len <= 0xFFFF) {
        header[1] = 126 | 0x80;
        header[2] = (uint8_t)(len >> 8);
        header[3] = (uint8_t)(len & 0xFF);
        header_len = 4;
    } else {
        header[1] = 127 | 0x80;
        for (int i = 0; i < 8; i++) {
            header[2 + i] = (uint8_t)((len >> (56 - 8 * i)) & 0xFF);
        }
        header_len = 10;
    }

    /* Generate mask */
    uint8_t mask[4];
    for (int i = 0; i < 4; i++) {
        mbedtls_ctr_drbg_random(&sig->ctr_drbg, &mask[i], 1);
    }

    /* Append mask after header */
    memcpy(header + header_len, mask, 4);
    header_len += 4;

    /* Build send buffer */
    uint8_t send_buf[SIG_MAX_SEND_BUF];
    size_t total_len = header_len + len;
    if (total_len > sizeof(send_buf)) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Send buffer too small: %zu > %zu", total_len, sizeof(send_buf));
        return -1;
    }

    memcpy(send_buf, header, header_len);
    memcpy(send_buf + header_len, data, len);

    /* Apply mask */
    for (size_t i = 0; i < len; i++) {
        send_buf[header_len + i] ^= mask[i % 4];
    }

    /* Send */
    int sent = 0;
    size_t remaining = total_len;
    bool is_wss = sig_ssl_configured(sig);
    while (remaining > 0) {
        int ret;
        if (is_wss) {
            ret = mbedtls_ssl_write(&sig->ssl, send_buf + sent, remaining);
        } else {
            ret = mbedtls_net_send(&sig->net, send_buf + sent, remaining);
        }

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            aosl_msleep(1);
            continue;
        }

        if (ret < 0) {
            snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                     "WebSocket send failed: %d", ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            return -1;
        }

        sent += ret;
        remaining -= ret;
    }

    return 0;
}

static int sig_configure_tls_client(struct tinyrtc_signaling *sig,
                                    bool ca_loaded,
                                    bool verify_disabled)
{
    int ret = mbedtls_ssl_config_defaults(&sig->conf,
                                          MBEDTLS_SSL_IS_CLIENT,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        return ret;
    }

    mbedtls_ssl_conf_authmode(&sig->conf,
                              (ca_loaded && !verify_disabled) ? MBEDTLS_SSL_VERIFY_REQUIRED
                                : MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_ca_chain(&sig->conf, &sig->cacert, NULL);
    mbedtls_ssl_conf_rng(&sig->conf, mbedtls_ctr_drbg_random, &sig->ctr_drbg);

    return 0;
}

/* Process incoming text message - parse JSON and invoke callback */
static void sig_process_message(struct tinyrtc_signaling *sig, const uint8_t *data, size_t len) {
    /* Null-terminate for easy parsing */
    char *json = (char *)aosl_malloc(len + 1);
    if (!json) {
        aosl_log(AOSL_LOG_ERROR, "Signaling: out of memory processing message");
        return;
    }
    memcpy(json, data, len);
    json[len] = '\0';

    /* The protocol envelope from the current sdp-transfer server is:
     * Simple flat JSON with a top-level "type" field:
     * {
     *   "type": "joined",
     *   "role": "caller"|"callee",
     *   "room": "room-id",
     *   "iceServers": [...]
     * }
     * {
     *   "type": "peer-joined"
     * }
     * {
     *   "type": "offer",
     *   "sdp": ...
     * }
     * {
     *   "type": "answer",
     *   "sdp": ...
     * }
     * {
     *   "type": "ice-candidate",
     *   "candidate": ...
     * }
     * {
     *   "type": "error",
     *   "message": "..."
     * }
     *
     * Important limitation:
     * - sdp-transfer's browser demo forwards RTCSessionDescription/RTCIceCandidate
     *   objects as JSON values.
     * - TinyRTC currently only extracts SDP reliably when it arrives as a plain
     *   string and does not fully parse forwarded ICE candidate objects yet.
     */

    /* Find the type field */
    char *type_ptr = strstr(json, "\"type\"");
    if (!type_ptr) {
        aosl_log(AOSL_LOG_WARNING, "Signaling: message missing type field");
        aosl_free(json);
        return;
    }

    /* Skip to the value */
    type_ptr = strchr(type_ptr + 6, ':');
    if (!type_ptr) {
        aosl_free(json);
        return;
    }

    while (*type_ptr && (*type_ptr == ':' || *type_ptr == ' ' || *type_ptr == '"')) {
        type_ptr++;
    }

    /* Now type_ptr points to the type value */

    /* Check message type */
    tinyrtc_signal_event_type_t event_type;
    bool have_type = false;

    if (strncmp(type_ptr, "joined", 6) == 0) {
        /* Server acknowledged join - success response, no callback needed */
        aosl_log(AOSL_LOG_INFO, "Signaling: joined room %s successfully", sig->room_id);
        aosl_free(json);
        return;
    } else if (strncmp(type_ptr, "peer-joined", 11) == 0) {
        event_type = TINYRTC_SIGNAL_EVENT_PEER_JOIN;
        have_type = true;
    } else if (strncmp(type_ptr, "peer-left", 9) == 0) {
        event_type = TINYRTC_SIGNAL_EVENT_PEER_LEAVE;
        have_type = true;
    } else if (strncmp(type_ptr, "offer", 5) == 0) {
        event_type = TINYRTC_SIGNAL_EVENT_OFFER;
        have_type = true;
    } else if (strncmp(type_ptr, "answer", 6) == 0) {
        event_type = TINYRTC_SIGNAL_EVENT_ANSWER;
        have_type = true;
    } else if (strncmp(type_ptr, "ice-candidate", 13) == 0) {
        event_type = TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE;
        have_type = true;
    } else if (strncmp(type_ptr, "error", 5) == 0) {
        /* Error message from server */
        char *msg_ptr = strstr(json, "\"message\"");
        if (msg_ptr) {
            msg_ptr = strchr(msg_ptr + 9, ':');
            if (msg_ptr) {
                while (*msg_ptr && (*msg_ptr == ':' || *msg_ptr == ' ' || *msg_ptr == '"')) {
                    msg_ptr++;
                }
                char *end = msg_ptr;
                while (*end && *end != '"' && *end != '}' && *end != ',') end++;
                *end = '\0';
                aosl_log(AOSL_LOG_ERROR, "Signaling: server error - %s", msg_ptr);
            }
        }
        aosl_free(json);
        return;
    }

    /* Invoke user callback */
    if (sig->callback) {
        tinyrtc_signal_event_t event;
        event.type = event_type;
        event.from_client_id = "server";

        /* Initialize pointers to NULL */
        event.data.offer = NULL;
        event.data.answer = NULL;
        event.data.candidate = NULL;

        /* Extract the actual data based on type */
        if (event_type == TINYRTC_SIGNAL_EVENT_PEER_JOIN) {
            aosl_log(AOSL_LOG_INFO, "Signaling: peer joined room");
        } else if (event_type == TINYRTC_SIGNAL_EVENT_PEER_LEAVE) {
            aosl_log(AOSL_LOG_INFO, "Signaling: peer left room");
        } else if (event_type == TINYRTC_SIGNAL_EVENT_OFFER || event_type == TINYRTC_SIGNAL_EVENT_ANSWER) {
            char *sdp = sig_extract_sdp_value(json);
            if (sdp != NULL) {
                if (event_type == TINYRTC_SIGNAL_EVENT_OFFER) {
                    event.data.offer = sdp;
                } else {
                    event.data.answer = sdp;
                }
                TINYRTC_LOG_DEBUG("Extracted SDP: %zu bytes", strlen(sdp));
            }
        } else if (event_type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE) {
            event.data.candidate = sig_extract_candidate_value(json);
        }

        sig->callback(&event, sig->user_data);

        /* Free any allocated data */
        if (event_type == TINYRTC_SIGNAL_EVENT_OFFER && event.data.offer != NULL) {
            aosl_free(event.data.offer);
        } else if (event_type == TINYRTC_SIGNAL_EVENT_ANSWER && event.data.answer != NULL) {
            aosl_free(event.data.answer);
        } else if (event_type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE && event.data.candidate != NULL) {
            sig_free_candidate(event.data.candidate);
        }
    }

    aosl_free(json);
}

/* Public API implementation */

tinyrtc_signaling_t *tinyrtc_signaling_create(
    tinyrtc_context_t *ctx,
    const tinyrtc_signaling_config_t *config,
    tinyrtc_signal_callback_t callback,
    void *user_data
) {
    (void)ctx; /* We keep the back reference but don't use it yet */

    if (!ctx || !config || !callback) {
        return NULL;
    }

    aosl_log(AOSL_LOG_DEBUG, "signaling: creating client instance... url=%s room=%s\n",
            config->url, config->room_id);

    tinyrtc_signaling_t *sig = (tinyrtc_signaling_t *)aosl_malloc(sizeof(*sig));
    if (!sig) {
        aosl_log(AOSL_LOG_ERROR, "signaling: out of memory allocating context");
        return NULL;
    }

    memset(sig, 0, sizeof(*sig));
    sig->tinyrtc_ctx = ctx;
    sig->callback = callback;
    sig->user_data = user_data;
    sig->state = TINYRTC_SIGNALING_DISCONNECTED;
    sig->message_buf = NULL;

    /* Copy configuration */
    if (config->url) {
        strncpy(sig->server_url, config->url, sizeof(sig->server_url) - 1);
    }
    if (config->room_id) {
        strncpy(sig->room_id, config->room_id, sizeof(sig->room_id) - 1);
    }
    sig->auto_connect = config->auto_connect;
    sig->disable_cert_verify = config->disable_cert_verify;

    /* Initialize mbedTLS */
    mbedtls_net_init(&sig->net);
    mbedtls_entropy_init(&sig->entropy);
    mbedtls_ctr_drbg_init(&sig->ctr_drbg);
    mbedtls_ssl_init(&sig->ssl);
    mbedtls_ssl_config_init(&sig->conf);
    mbedtls_x509_crt_init(&sig->cacert);

    aosl_log(AOSL_LOG_DEBUG, "Signaling: mbedTLS structures initialized\n");

    const char *pers = "tinyrtc-signaling";
    int ret = mbedtls_ctr_drbg_seed(&sig->ctr_drbg, mbedtls_entropy_func,
                                     &sig->entropy,
                                     (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    /* Generate client ID after random number generator is seeded */
    if (config->client_id) {
        strncpy(sig->client_id, config->client_id, sizeof(sig->client_id) - 1);
    } else {
        sig_generate_client_id(sig, sig->client_id, sizeof(sig->client_id));
    }

    aosl_log(AOSL_LOG_DEBUG, "Signaling: CTR-DRBG seeded\n");

    /* Load system CA bundle for certificate verification */
    bool ca_loaded = false;
    const char *ca_paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/usr/share/ca-certificates/ca-certificates.crt",
        NULL
    };

    /* Check if certificate verification is disabled */
    bool verify_disabled = sig->disable_cert_verify;

    if (!verify_disabled) {
        for (int i = 0; ca_paths[i] != NULL; i++) {
            ret = mbedtls_x509_crt_parse_file(&sig->cacert, ca_paths[i]);
            if (ret == 0) {
                ca_loaded = true;
                break;
            }
        }

        if (!ca_loaded) {
            aosl_log(AOSL_LOG_WARNING, "Signaling: Could not load CA certificates, "
                                    "disabling certificate verification");
        } else {
            aosl_log(AOSL_LOG_DEBUG, "Signaling: CA certificates loaded\n");
        }
    } else {
        aosl_log(AOSL_LOG_INFO, "Signaling: SSL certificate verification disabled\n");
    }

    /* Parse URL */
    char host[128];
    uint16_t port;
    if (sig_parse_url(sig->server_url, host, sizeof(host), &port, &sig->is_wss) != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Invalid URL: %s", sig->server_url);
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_DEBUG, "Signaling: URL parsed host=%s port=%d is_wss=%d\n",
            host, port, sig->is_wss);

    /* Connect TCP socket */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    ret = mbedtls_net_connect(&sig->net, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                 "Failed to connect to %s:%s: -0x%04x", host, port_str, -ret);
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_INFO, "Signaling: TCP connected to %s:%s\n", host, port_str);

    if (sig->is_wss) {
        /* Setup SSL */
        bool verify_disabled = sig->disable_cert_verify;
        ret = sig_configure_tls_client(sig, ca_loaded, verify_disabled);
        if (ret != 0) {
            snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                     "mbedtls_ssl_config_defaults failed: -0x%04x", -ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            goto error;
        }

        ret = mbedtls_ssl_setup(&sig->ssl, &sig->conf);
        if (ret != 0) {
            snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                     "mbedtls_ssl_setup failed: -0x%04x", -ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            goto error;
        }

        mbedtls_ssl_set_hostname(&sig->ssl, host);
        mbedtls_ssl_set_bio(&sig->ssl, &sig->net, mbedtls_net_send,
                              mbedtls_net_recv, NULL);

        /* SSL handshake - use blocking mode during handshake */
        aosl_log(AOSL_LOG_DEBUG, "Signaling: Starting SSL handshake...\n");
        do {
            ret = mbedtls_ssl_handshake(&sig->ssl);
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret != 0) {
            snprintf(sig->last_error, sizeof(sig->last_error) - 1,
                     "SSL handshake failed: -0x%04x", -ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            goto error;
        }

        aosl_log(AOSL_LOG_DEBUG, "Signaling: SSL handshake completed successfully\n");

        /* Verify certificate */
        if (ca_loaded) {
            ret = mbedtls_ssl_get_verify_result(&sig->ssl);
            if (ret != 0) {
                char vrfy_buf[512];
                mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf),
                                           "  ", ret);
                aosl_log(AOSL_LOG_WARNING, "Signaling: Certificate verification failed:\n%s",
                         vrfy_buf);
                /* Continue anyway for demo purposes */
            }
        }

        aosl_log(AOSL_LOG_DEBUG, "Signaling: SSL handshake completed\n");
    }

    /* Set socket to non-blocking mode so that mbedtls_net_recv returns immediately when no data
     * is available, allowing the main loop to continue processing ICE connectivity checks,
     * send STUN pings, process DTLS, etc. Without this, the main loop will block forever
     * waiting for new WebSocket messages and ICE connectivity will never progress. */
    int flags = fcntl(sig->net.fd, F_GETFL, 0);
    fcntl(sig->net.fd, F_SETFL, flags | O_NONBLOCK);

    sig->state = TINYRTC_SIGNALING_CONNECTING;

    /* Perform WebSocket handshake */
    if (sig_perform_websocket_handshake(sig, host, "/") != 0) {
        /* Error already set */
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    sig->state = TINYRTC_SIGNALING_CONNECTED;

    /* Send join message according to protocol */
    char join_json[512];
    snprintf(join_json, sizeof(join_json),
             "{\"type\": \"join\", \"room\": \"%s\"}",
             sig->room_id);
    aosl_log(AOSL_LOG_DEBUG, "Signaling: sending join request: %s\n", join_json);

    ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT,
                           (const unsigned char *)join_json, strlen(join_json));
    if (ret != 0) {
        /* Error already set in sig_send_ws_frame */
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_INFO, "Signaling: connected successfully, room=%s client=%s\n",
            sig->room_id, sig->client_id);

    return sig;

error:
    if (sig && sig->state == TINYRTC_SIGNALING_ERROR) {
        TINYRTC_LOG_ERROR("%s", sig->last_error);
        mbedtls_x509_crt_free(&sig->cacert);
        mbedtls_ssl_config_free(&sig->conf);
        mbedtls_ssl_free(&sig->ssl);
        mbedtls_ctr_drbg_free(&sig->ctr_drbg);
        mbedtls_entropy_free(&sig->entropy);
        mbedtls_net_free(&sig->net);
        if (sig->message_buf) {
            aosl_free(sig->message_buf);
        }
        aosl_free(sig);
    }
    return NULL;
}

void tinyrtc_signaling_destroy(tinyrtc_signaling_t *sig) {
    if (!sig) return;

    if (sig->state == TINYRTC_SIGNALING_CONNECTED) {
        /* Send close frame */
        sig_send_ws_frame(sig, WS_OPCODE_CLOSE, NULL, 0);
    }

    mbedtls_x509_crt_free(&sig->cacert);
    mbedtls_ssl_config_free(&sig->conf);
    mbedtls_ssl_free(&sig->ssl);
    mbedtls_ctr_drbg_free(&sig->ctr_drbg);
    mbedtls_entropy_free(&sig->entropy);
    mbedtls_net_free(&sig->net);

    if (sig->message_buf) {
        aosl_free(sig->message_buf);
    }

    aosl_free(sig);
}

tinyrtc_error_t tinyrtc_signaling_connect(tinyrtc_signaling_t *sig) {
    if (!sig) return TINYRTC_ERROR_INVALID_ARG;

    /* Already connected during create because we need handshake to complete */
    return TINYRTC_OK;
}

void tinyrtc_signaling_disconnect(tinyrtc_signaling_t *sig) {
    if (!sig) return;
    sig->state = TINYRTC_SIGNALING_DISCONNECTED;
    /* TCP/SSL close is done in destroy */
}

tinyrtc_signaling_state_t tinyrtc_signaling_get_state(tinyrtc_signaling_t *sig) {
    if (!sig) return TINYRTC_SIGNALING_DISCONNECTED;
    return sig->state;
}

/* Helper: JSON escape a string into buffer */
static int sig_json_escape(const char *input, char *output, size_t output_size) {
    size_t out_len = 0;
    while (*input && out_len < output_size - 1) {
        switch (*input) {
            case '"':
                if (out_len + 2 >= output_size) break;
                output[out_len++] = '\\';
                output[out_len++] = '"';
                break;
            case '\\':
                if (out_len + 2 >= output_size) break;
                output[out_len++] = '\\';
                output[out_len++] = '\\';
                break;
            case '\n':
                if (out_len + 2 >= output_size) break;
                output[out_len++] = '\\';
                output[out_len++] = 'n';
                break;
            case '\r':
                if (out_len + 2 >= output_size) break;
                output[out_len++] = '\\';
                output[out_len++] = 'r';
                break;
            case '\t':
                if (out_len + 2 >= output_size) break;
                output[out_len++] = '\\';
                output[out_len++] = 't';
                break;
            default:
                output[out_len++] = *input;
                break;
        }
        input++;
    }
    output[out_len] = '\0';
    return (int)out_len;
}

tinyrtc_error_t tinyrtc_signaling_send_offer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp)
{
    (void)to_client_id; /* We broadcast to room, not direct */

    if (!sig || sig->state != TINYRTC_SIGNALING_CONNECTED || !sdp) {
        return TINYRTC_ERROR_INVALID_ARG;
    }

    /* Build JSON according to current sdp-transfer expected format - simple flat format */
    /* Need to escape SDP because it contains newlines */
    char json[8192];
    char escaped_sdp[4096];
    sig_json_escape(sdp, escaped_sdp, sizeof(escaped_sdp));

    int len = snprintf(json, sizeof(json),
        "{\"type\": \"offer\", \"sdp\": \"%s\"}",
        escaped_sdp
    );

    if (len >= (int)sizeof(json)) {
        return TINYRTC_ERROR_MEMORY;
    }

    int ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT, (const uint8_t *)json, len);
    if (ret == 0) {
        aosl_log(AOSL_LOG_DEBUG, "Signaling: sent offer (%d bytes)\n", len);
    }
    return ret == 0 ? TINYRTC_OK : TINYRTC_ERROR_NETWORK;
}

tinyrtc_error_t tinyrtc_signaling_send_answer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp)
{
    (void)to_client_id; /* We broadcast to room, not direct */

    if (!sig || sig->state != TINYRTC_SIGNALING_CONNECTED || !sdp) {
        return TINYRTC_ERROR_INVALID_ARG;
    }

    /* Build JSON according to current sdp-transfer expected format - simple flat format */
    /* Need to escape SDP because it contains newlines */
    char json[8192];
    char escaped_sdp[4096];
    sig_json_escape(sdp, escaped_sdp, sizeof(escaped_sdp));

    int len = snprintf(json, sizeof(json),
        "{\"type\": \"answer\", \"sdp\": \"%s\"}",
        escaped_sdp
    );

    if (len >= (int)sizeof(json)) {
        return TINYRTC_ERROR_MEMORY;
    }

    int ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT, (const uint8_t *)json, len);
    if (ret == 0) {
        aosl_log(AOSL_LOG_DEBUG, "Signaling: sent answer (%d bytes)\n", len);
    }
    return ret == 0 ? TINYRTC_OK : TINYRTC_ERROR_NETWORK;
}

tinyrtc_error_t tinyrtc_signaling_send_candidate(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    tinyrtc_ice_candidate_t *candidate)
{
    (void)to_client_id;
    (void)candidate;

    if (!sig || sig->state != TINYRTC_SIGNALING_CONNECTED || !candidate) {
        return TINYRTC_ERROR_INVALID_ARG;
    }

    /* TODO: implement when we have full ICE candidate support in TinyRTC */
    aosl_log(AOSL_LOG_DEBUG, "Signaling: ICE candidate sending not implemented yet\n");
    return TINYRTC_OK;
}

int tinyrtc_signaling_process(tinyrtc_signaling_t *sig)
{
    if (!sig || sig->state != TINYRTC_SIGNALING_CONNECTED) {
        return 0;
    }

    int events_processed = 0;

    /* Read as much as possible */
    while (true) {
        int ret = sig_read_ws_frame(sig);
        if (ret < 0) {
            /* Error, close connection */
            sig->state = TINYRTC_SIGNALING_DISCONNECTED;
            break;
        } else if (ret == 0) {
            /* No more data available right now */
            break;
        }
        events_processed++;
    }

    return events_processed;
}
