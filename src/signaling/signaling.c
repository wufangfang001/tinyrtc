/**
 * @file signaling.c
 * @brief Signaling client implementation for WebSocket-based SDP exchange
 */

#include "tinyrtc/signaling.h"
#include "common.h"
#include "api/aosl.h"
#include <tinyrtc/tinyrtc.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

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
static void sig_process_message(struct tinyrtc_signaling *sig, const uint8_t *data, size_t len);

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

/* Generate WebSocket accept key according to RFC6455 */
static void sig_compute_accept_key(const char *client_key, char *accept, size_t accept_len) {
    /* The GUID from RFC6455 */
    static const char *guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    size_t concat_len = strlen(client_key) + strlen(guid);
    unsigned char concat[512]; // client-key is at most 24 chars, guid is 36 → total 60 < 512
    
    memcpy(concat, client_key, strlen(client_key));
    memcpy(concat + strlen(client_key), guid, strlen(guid));

    /* SHA-1 hash using mbedTLS */
    unsigned char hash[20];
#if MBEDTLS_VERSION_MAJOR >= 3
    mbedtls_sha1(concat, concat_len, hash);
#else
    mbedtls_sha1_context ctx;
    mbedtls_sha1_init(&ctx);
    mbedtls_sha1_starts(&ctx);
    mbedtls_sha1_update(&ctx, concat, concat_len);
    mbedtls_sha1_finish(&ctx, hash);
#endif

    /* Base64 encode
     * SHA-1 always outputs exactly 20 bytes (160 bits)
     * Use bit accumulation approach that's guaranteed to be correct
     */
    static const char *b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    int bits = 0;
    uint32_t accum = 0;
    int j = 0;

    for (int i = 0; i < 20; i++) {
        accum = (accum << 8) | hash[i];
        bits += 8;
        while (bits >= 6 && j + 1 < (int)accept_len) {
            bits -= 6;
            accept[j++] = b64[(accum >> bits) & 0x3F];
        }
    }

    // Add remaining bits if any
    if (bits > 0 && j + 1 < (int)accept_len) {
        accum <<= (6 - bits);
        accept[j++] = b64[accum & 0x3F];
    }

    // Add padding to make output length multiple of 4
    while (j % 4 != 0 && j + 1 < (int)accept_len) {
        accept[j++] = '=';
    }

    accept[j] = '\0';
}

static int sig_perform_websocket_handshake(struct tinyrtc_signaling *sig,
                                            const char *host, const char *path) {
    /* Generate a random Sec-WebSocket-Key */
    unsigned char key_bytes[16];
    for (int i = 0; i < 16; i++) {
        mbedtls_ctr_drbg_random(&sig->ctr_drbg, &key_bytes[i], 1);
    }

    /* Base64 encode the key */
    static const char *b64 =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    char client_key[28]; /* 16 bytes -> 24 base64 chars + null */
    int i = 0;
    int j = 0;
    unsigned char chunk[3];
    while (i < 16) {
        chunk[0] = key_bytes[i++];
        chunk[1] = i < 16 ? key_bytes[i++] : 0;
        chunk[2] = i < 16 ? key_bytes[i++] : 0;

        client_key[j++] = b64[(chunk[0] >> 2) & 0x3F];
        client_key[j++] = b64[((chunk[0] & 0x03) << 4) | ((chunk[1] >> 4) & 0x0F)];
        client_key[j++] = b64[((chunk[1] & 0x0F) << 2) | ((chunk[2] >> 6) & 0x03)];
        client_key[j++] = b64[chunk[2] & 0x3F];
    }
    client_key[j] = '\0';

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
        snprintf(sig->last_error, sizeof(sig->last_error),
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
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "Bad handshake response: expected 101, got:\n%.100s", response);
        return -1;
    }

    /* Check Sec-WebSocket-Accept */
    char expected_accept[32];
    sig_compute_accept_key(client_key, expected_accept, sizeof(expected_accept));

    char *accept_header = strstr(response, "Sec-WebSocket-Accept:");
    if (!accept_header) {
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "No Sec-WebSocket-Accept in response");
        return -1;
    }

    accept_header += strlen("Sec-WebSocket-Accept:");
    while (*accept_header == ' ' || *accept_header == '\t') {
        accept_header++;
    }

    char *accept_end = strchr(accept_header, '\r');
    if (accept_end) {
        *accept_end = '\0';
    }

    if (strcmp(accept_header, expected_accept) != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "Invalid Sec-WebSocket-Accept: expected %s, got %s",
                 expected_accept, accept_header);
        return -1;
    }

    aosl_log(AOSL_LOG_DEBUG, "Signaling: WebSocket handshake completed");
    return 0;
}

static int sig_read_ws_frame(struct tinyrtc_signaling *sig) {
    /* Read header */
    uint8_t hdr[2];
    int ret;

    bool is_wss = sig_ssl_configured(sig);

    for (int i = 0; i < 2; i++) {
        do {
            if (is_wss) {
                ret = mbedtls_ssl_read(&sig->ssl, &hdr[i], 1);
            } else {
                ret = mbedtls_net_recv(&sig->net, &hdr[i], 1);
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                aosl_msleep(1);
            }
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret <= 0) {
            /* No data available, not an error for polling */
            return 0;
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
            do {
                if (is_wss) {
                    ret = mbedtls_ssl_read(&sig->ssl, &ext_len[i], 1);
                } else {
                    ret = mbedtls_net_recv(&sig->net, &ext_len[i], 1);
                }
                if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    aosl_msleep(1);
                }
            } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                     ret == MBEDTLS_ERR_SSL_WANT_WRITE);

            if (ret <= 0) {
                snprintf(sig->last_error, sizeof(sig->last_error),
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
            do {
                if (is_wss) {
                    ret = mbedtls_ssl_read(&sig->ssl, &mask_key[i], 1);
                } else {
                    ret = mbedtls_net_recv(&sig->net, &mask_key[i], 1);
                }
                if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    aosl_msleep(1);
                }
            } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                     ret == MBEDTLS_ERR_SSL_WANT_WRITE);

            if (ret <= 0) {
                snprintf(sig->last_error, sizeof(sig->last_error),
                         "Failed to read mask key");
                return -1;
            }
        }
    }

    /* Ensure we have buffer space */
    if (len > SIG_MAX_RECV_BUF) {
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "WebSocket frame too large: %zu bytes (max %d)",
                 len, SIG_MAX_RECV_BUF);
        return -1;
    }

    /* Read payload */
    for (size_t i = 0; i < len; i++) {
        do {
            if (is_wss) {
                ret = mbedtls_ssl_read(&sig->ssl, &sig->recv_buf[sig->recv_len + i], 1);
            } else {
                ret = mbedtls_net_recv(&sig->net, &sig->recv_buf[sig->recv_len + i], 1);
            }
            if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
                aosl_msleep(1);
            }
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret <= 0) {
            snprintf(sig->last_error, sizeof(sig->last_error),
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
                        snprintf(sig->last_error, sizeof(sig->last_error),
                                 "Out of memory for fragmented message");
                        return -1;
                    }
                    sig->message_len = 0;
                }

                /* Append to buffer */
                if (sig->message_len + len > sig->message_cap) {
                    snprintf(sig->last_error, sizeof(sig->last_error),
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
                snprintf(sig->last_error, sizeof(sig->last_error),
                         "Invalid state: fragmented message without buffer");
                return -1;
            }
            if (sig->message_len + len > sig->message_cap) {
                snprintf(sig->last_error, sizeof(sig->last_error),
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
            aosl_log(AOSL_LOG_INFO, "Signaling: received close frame from server");
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
            aosl_log(AOSL_LOG_WARNING, "Signaling: unknown WebSocket opcode 0x%02x", opcode);
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
        snprintf(sig->last_error, sizeof(sig->last_error),
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
            snprintf(sig->last_error, sizeof(sig->last_error),
                     "WebSocket send failed: %d", ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            return -1;
        }

        sent += ret;
        remaining -= ret;
    }

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

    aosl_log(AOSL_LOG_DEBUG, "Signaling: received message: %.100s...", json);

    /* The protocol format from WebRTC-Experiment is:
     * {
     *   "sender": "sender-id",
     *   "channel": "channel-id",
     *   "message": { ... actual message ... }
     * }
     *
     * Or for initial presence check it's:
     * {
     *   "isChannelPresent": false
     * }
     */

    /* Check for presence check response */
    if (strstr(json, "\"isChannelPresent\"") != NULL) {
        /* This is just the presence response, ignore it */
        aosl_log(AOSL_LOG_DEBUG, "Signaling: got presence check response");
        aosl_free(json);
        return;
    }

    /* Check if this message is for our channel and from another sender */
    /* We need to find "channel" and "sender" */
    char *channel_ptr = strstr(json, "\"channel\"");
    if (!channel_ptr) {
        aosl_free(json);
        return;
    }

    char *sender_ptr = strstr(json, "\"sender\"");
    if (!sender_ptr) {
        aosl_free(json);
        return;
    }

    /* Skip to the value */
    channel_ptr = strchr(channel_ptr + 8, ':');
    sender_ptr = strchr(sender_ptr + 7, ':');
    if (!channel_ptr || !sender_ptr) {
        aosl_free(json);
        return;
    }

    /* Extract values (very simplistic parsing) */
    while (*channel_ptr && (*channel_ptr == ':' || *channel_ptr == ' ' || *channel_ptr == '\"')) {
        channel_ptr++;
    }
    while (*sender_ptr && (*sender_ptr == ':' || *sender_ptr == ' ' || *sender_ptr == '\"')) {
        sender_ptr++;
    }

    /* Check if this is our channel and not from us */
    if (strncmp(channel_ptr, sig->room_id, strlen(sig->room_id)) != 0) {
        aosl_free(json);
        return;
    }

    if (strncmp(sender_ptr, sig->client_id, strlen(sig->client_id)) == 0) {
        /* Ignore our own messages */
        aosl_free(json);
        return;
    }

    /* Find the message field */
    char *message_ptr = strstr(json, "\"message\"");
    if (!message_ptr) {
        aosl_free(json);
        return;
    }

    /* Find where the message object starts */
    message_ptr = strchr(message_ptr + 8, '{');
    if (!message_ptr) {
        aosl_free(json);
        return;
    }

    /* Check message type */
    tinyrtc_signal_event_type_t event_type;
    bool have_type = false;

    if (strstr(message_ptr, "\"type\"") && strstr(message_ptr, "\"offer\"")) {
        event_type = TINYRTC_SIGNAL_EVENT_OFFER;
        have_type = true;
    } else if (strstr(message_ptr, "\"type\"") && strstr(message_ptr, "\"answer\"")) {
        event_type = TINYRTC_SIGNAL_EVENT_ANSWER;
        have_type = true;
    } else if (strstr(message_ptr, "\"type\"") && strstr(message_ptr, "\"candidate\"")) {
        event_type = TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE;
        have_type = true;
    }

    if (!have_type) {
        aosl_free(json);
        return;
    }

    /* Invoke user callback */
    if (sig->callback) {
        tinyrtc_signal_event_t event;
        event.type = event_type;
        event.from_client_id = sender_ptr;

        /* Initialize pointers to NULL */
        event.data.offer = NULL;
        event.data.answer = NULL;
        event.data.candidate = NULL;

        /* Extract the actual data based on type */
        if (event_type == TINYRTC_SIGNAL_EVENT_OFFER || event_type == TINYRTC_SIGNAL_EVENT_ANSWER) {
            char *found = strstr(message_ptr, "\"sdp\"");
            if (found) {
                found = strchr(found + 5, ':');
                if (found) {
                    found++;
                    while (*found && (*found == ' ' || *found == '\"')) {
                        found++;
                    }
                    char *end = found;
                    while (*end && *end != '\"' && *end != '}' && *end != ',') {
                        end++;
                    }
                    size_t sdp_len = (size_t)(end - found);
                    char *sdp = (char *)aosl_malloc(sdp_len + 1);
                    if (sdp) {
                        memcpy(sdp, found, sdp_len);
                        sdp[sdp_len] = '\0';
                        if (event_type == TINYRTC_SIGNAL_EVENT_OFFER) {
                            event.data.offer = sdp;
                        } else {
                            event.data.answer = sdp;
                        }
                    }
                }
            }
        } else if (event_type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE) {
            /* For now we just leave it as NULL since we need parsing support */
            event.data.candidate = NULL;
        }

        sig->callback(&event, sig->user_data);

        /* Free any allocated data */
        if (event_type == TINYRTC_SIGNAL_EVENT_OFFER && event.data.offer != NULL) {
            aosl_free(event.data.offer);
        } else if (event_type == TINYRTC_SIGNAL_EVENT_ANSWER && event.data.answer != NULL) {
            aosl_free(event.data.answer);
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

    aosl_log(AOSL_LOG_DEBUG, "signaling: creating client instance... url=%s room=%s",
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

    /* Initialize mbedTLS */
    mbedtls_net_init(&sig->net);
    mbedtls_entropy_init(&sig->entropy);
    mbedtls_ctr_drbg_init(&sig->ctr_drbg);
    mbedtls_ssl_init(&sig->ssl);
    mbedtls_ssl_config_init(&sig->conf);
    mbedtls_x509_crt_init(&sig->cacert);

    aosl_log(AOSL_LOG_DEBUG, "Signaling: mbedTLS structures initialized");

    const char *pers = "tinyrtc-signaling";
    int ret = mbedtls_ctr_drbg_seed(&sig->ctr_drbg, mbedtls_entropy_func,
                                     &sig->entropy,
                                     (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error),
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

    aosl_log(AOSL_LOG_DEBUG, "Signaling: CTR-DRBG seeded");

    aosl_log(AOSL_LOG_DEBUG, "Signaling: CTR-DRBG seeded");

    /* Load system CA bundle for certificate verification */
    bool ca_loaded = false;
    const char *ca_paths[] = {
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/usr/share/ca-certificates/ca-certificates.crt",
        NULL
    };

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
        aosl_log(AOSL_LOG_DEBUG, "Signaling: CA certificates loaded");
    }

    /* Parse URL */
    char host[128];
    uint16_t port;
    if (sig_parse_url(sig->server_url, host, sizeof(host), &port, &sig->is_wss) != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "Invalid URL: %s", sig->server_url);
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_DEBUG, "Signaling: URL parsed host=%s port=%d is_wss=%d",
            host, port, sig->is_wss);

    /* Connect TCP socket */
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", port);
    ret = mbedtls_net_connect(&sig->net, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) {
        snprintf(sig->last_error, sizeof(sig->last_error),
                 "Failed to connect to %s:%s: -0x%04x", host, port_str, -ret);
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_INFO, "Signaling: TCP connected to %s:%s", host, port_str);

    if (sig->is_wss) {
        /* Setup SSL */
        mbedtls_ssl_conf_authmode(&sig->conf,
                                  ca_loaded ? MBEDTLS_SSL_VERIFY_REQUIRED
                                            : MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_ca_chain(&sig->conf, &sig->cacert, NULL);
        mbedtls_ssl_conf_rng(&sig->conf, mbedtls_ctr_drbg_random,
                              &sig->ctr_drbg);

        ret = mbedtls_ssl_setup(&sig->ssl, &sig->conf);
        if (ret != 0) {
            snprintf(sig->last_error, sizeof(sig->last_error),
                     "mbedtls_ssl_setup failed: -0x%04x", -ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            goto error;
        }

        mbedtls_ssl_set_hostname(&sig->ssl, host);
        mbedtls_ssl_set_bio(&sig->ssl, &sig->net, mbedtls_net_send,
                              mbedtls_net_recv, NULL);

        /* SSL handshake */
        do {
            ret = mbedtls_ssl_handshake(&sig->ssl);
        } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
                 ret == MBEDTLS_ERR_SSL_WANT_WRITE);

        if (ret != 0) {
            snprintf(sig->last_error, sizeof(sig->last_error),
                     "SSL handshake failed: -0x%04x", -ret);
            sig->state = TINYRTC_SIGNALING_ERROR;
            goto error;
        }

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

        aosl_log(AOSL_LOG_DEBUG, "Signaling: SSL handshake completed");
    }

    sig->state = TINYRTC_SIGNALING_CONNECTING;

    /* Perform WebSocket handshake */
    if (sig_perform_websocket_handshake(sig, host, "/") != 0) {
        /* Error already set */
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    sig->state = TINYRTC_SIGNALING_CONNECTED;

    /* Send presence check according to protocol */
    char presence_json[512];
    snprintf(presence_json, sizeof(presence_json),
             "{\"checkPresence\": true, \"channel\": \"%s\"}",
             sig->room_id);
    aosl_log(AOSL_LOG_DEBUG, "Signaling: sending presence check: %s", presence_json);

    ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT,
                           (const unsigned char *)presence_json, strlen(presence_json));
    if (ret != 0) {
        /* Error already set in sig_send_ws_frame */
        sig->state = TINYRTC_SIGNALING_ERROR;
        goto error;
    }

    aosl_log(AOSL_LOG_INFO, "Signaling: connected successfully, room=%s client=%s",
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

    /* Build JSON according to expected format */
    /* Need to escape SDP because it contains newlines */
    char json[8192];
    char escaped_sdp[4096];
    int escaped_len = sig_json_escape(sdp, escaped_sdp, sizeof(escaped_sdp));

    int len = snprintf(json, sizeof(json),
        "{\n"
        "  \"sender\": \"%s\",\n"
        "  \"channel\": \"%s\",\n"
        "  \"message\": {\n"
        "    \"type\": \"offer\",\n"
        "    \"sdp\": \"%s\"\n"
        "  }\n"
        "}",
        sig->client_id, sig->room_id, escaped_sdp
    );

    if (len >= (int)sizeof(json)) {
        return TINYRTC_ERROR_MEMORY;
    }

    int ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT, (const uint8_t *)json, len);
    if (ret == 0) {
        aosl_log(AOSL_LOG_DEBUG, "Signaling: sent offer (%d bytes)", len);
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

    /* Build JSON according to expected format */
    /* Need to escape SDP because it contains newlines */
    char json[8192];
    char escaped_sdp[4096];
    int escaped_len = sig_json_escape(sdp, escaped_sdp, sizeof(escaped_sdp));

    int len = snprintf(json, sizeof(json),
        "{\n"
        "  \"sender\": \"%s\",\n"
        "  \"channel\": \"%s\",\n"
        "  \"message\": {\n"
        "    \"type\": \"answer\",\n"
        "    \"sdp\": \"%s\"\n"
        "  }\n"
        "}",
        sig->client_id, sig->room_id, escaped_sdp
    );

    if (len >= (int)sizeof(json)) {
        return TINYRTC_ERROR_MEMORY;
    }

    int ret = sig_send_ws_frame(sig, WS_OPCODE_TEXT, (const uint8_t *)json, len);
    if (ret == 0) {
        aosl_log(AOSL_LOG_DEBUG, "Signaling: sent answer (%d bytes)", len);
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
    aosl_log(AOSL_LOG_DEBUG, "Signaling: ICE candidate sending not implemented yet");
    return TINYRTC_OK;
}
