/**
 * @file signaling.h
 * @brief Signaling connection - connects to signaling server for SDP exchange
 *
 * TinyRTC supports two signaling modes:
 * 1. Manual signaling - application handles SDP/ICE exchange (default)
 * 2. Built-in signaling - connects to a signaling server automatically
 */

#ifndef TINYRTC_SIGNALING_H
#define TINYRTC_SIGNALING_H

#include "tinyrtc/tinyrtc.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signaling server configuration
 */
typedef struct {
    char *url;              /**< Signaling server URL (e.g., "wss://example.com/signal") */
    char *room_id;         /**< Room/channel ID to join */
    char *client_id;       /**< Optional client ID (auto-generated if NULL) */
    bool auto_connect;     /**< Auto-connect on creation */
} tinyrtc_signaling_config_t;

/**
 * @brief Signaling connection state
 */
typedef enum {
    TINYRTC_SIGNALING_DISCONNECTED = 0,
    TINYRTC_SIGNALING_CONNECTING,
    TINYRTC_SIGNALING_CONNECTED,
    TINYRTC_SIGNALING_ERROR,
} tinyrtc_signaling_state_t;

/**
 * @brief Signaling event types
 */
typedef enum {
    TINYRTC_SIGNAL_EVENT_OFFER = 0,      /**< Received offer from remote */
    TINYRTC_SIGNAL_EVENT_ANSWER = 1,     /**< Received answer from remote */
    TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE = 2, /**< Received ICE candidate */
    TINYRTC_SIGNAL_EVENT_PEER_JOIN = 3,   /**< Remote peer joined */
    TINYRTC_SIGNAL_EVENT_PEER_LEAVE = 4,  /**< Remote peer left */
} tinyrtc_signal_event_type_t;

/**
 * @brief Signaling event data
 */
typedef struct {
    tinyrtc_signal_event_type_t type;
    char *from_client_id;
    union {
        char *offer;           /**< SDP offer */
        char *answer;          /**< SDP answer */
        tinyrtc_ice_candidate_t *candidate; /**< ICE candidate */
    } data;
} tinyrtc_signal_event_t;

/**
 * @brief Signaling event callback
 *
 * Called when an event is received from the signaling server.
 */
typedef void (*tinyrtc_signal_callback_t)(
    tinyrtc_signal_event_t *event,
    void *user_data);

/**
 * @brief Signaling connection handle
 */
typedef struct tinyrtc_signaling tinyrtc_signaling_t;

/**
 * @brief Create a new signaling connection
 *
 * @param ctx TinyRTC context
 * @param config Signaling configuration
 * @param callback Event callback
 * @param user_data User context for callback
 * @return New signaling handle, NULL on error
 */
tinyrtc_signaling_t *tinyrtc_signaling_create(
    tinyrtc_context_t *ctx,
    const tinyrtc_signaling_config_t *config,
    tinyrtc_signal_callback_t callback,
    void *user_data);

/**
 * @brief Destroy signaling connection
 *
 * @param sig Signaling connection to destroy
 */
void tinyrtc_signaling_destroy(tinyrtc_signaling_t *sig);

/**
 * @brief Connect to signaling server
 *
 * @param sig Signaling connection
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_signaling_connect(tinyrtc_signaling_t *sig);

/**
 * @brief Disconnect from signaling server
 *
 * @param sig Signaling connection
 */
void tinyrtc_signaling_disconnect(tinyrtc_signaling_t *sig);

/**
 * @brief Get current connection state
 *
 * @param sig Signaling connection
 * @return Current state
 */
tinyrtc_signaling_state_t tinyrtc_signaling_get_state(tinyrtc_signaling_t *sig);

/**
 * @brief Process pending incoming messages from signaling server
 *
 * This should be called periodically from the main loop
 *
 * @param sig Signaling connection
 * @return Number of events processed
 */
int tinyrtc_signaling_process(tinyrtc_signaling_t *sig);

/**
 * @brief Send offer to remote peer
 *
 * @param sig Signaling connection
 * @param to_client_id Destination client ID
 * @param sdp SDP offer
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_signaling_send_offer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp);

/**
 * @brief Send answer to remote peer
 *
 * @param sig Signaling connection
 * @param to_client_id Destination client ID
 * @param sdp SDP answer
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_signaling_send_answer(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    const char *sdp);

/**
 * @brief Send ICE candidate to remote peer
 *
 * @param sig Signaling connection
 * @param to_client_id Destination client ID
 * @param candidate ICE candidate
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_signaling_send_candidate(
    tinyrtc_signaling_t *sig,
    const char *to_client_id,
    tinyrtc_ice_candidate_t *candidate);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_SIGNALING_H */
