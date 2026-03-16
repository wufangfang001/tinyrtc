/**
 * @file peer_connection.h
 * @brief TinyRTC PeerConnection - Main peer-to-peer connection interface
 *
 * This is the primary interface for creating and managing WebRTC connections.
 * Applications use this to establish connections, add media tracks, and send/receive
 * audio/video data.
 */

#ifndef TINYRTC_PEER_CONNECTION_H
#define TINYRTC_PEER_CONNECTION_H

#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/media_codec.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Peer connection states
 */
typedef enum {
    TINYRTC_PC_STATE_NEW = 0,
    TINYRTC_PC_STATE_CONNECTING,
    TINYRTC_PC_STATE_CONNECTED,
    TINYRTC_PC_STATE_DISCONNECTED,
    TINYRTC_PC_STATE_FAILED,
    TINYRTC_PC_STATE_CLOSED,
} tinyrtc_pc_state_t;

/**
 * @brief Track kinds (media types)
 */
typedef enum {
    TINYRTC_TRACK_KIND_AUDIO = 0,
    TINYRTC_TRACK_KIND_VIDEO = 1,
} tinyrtc_track_kind_t;

/**
 * @brief ICE candidate - represents a potential network candidate for connectivity
 */
typedef struct {
    char *foundation;
    uint32_t priority;
    char *ip;
    uint16_t port;
    char *type;          /**< "host", "srflx", "relay" */
    char *protocol;      /**< "udp", "tcp" */
    bool is_ipv6;
} tinyrtc_ice_candidate_t;

/**
 * @brief Media track configuration when adding a local track
 */
typedef struct {
    tinyrtc_track_kind_t kind;
    const char *mid;       /**< Media stream identifier */
    tinyrtc_codec_id_t codec_id;  /**< Codec ID */
    int payload_type;      /**< RTP payload type (0=use default from codec) */
    uint32_t clock_rate;   /**< RTP clock rate (0=use default from codec) */
} tinyrtc_track_config_t;

/**
 * @brief Media track handle - represents an audio or video track
 */
typedef struct tinyrtc_track tinyrtc_track_t;

/**
 * @brief PeerConnection event callbacks
 *
 * All events are delivered to the application through these callbacks.
 * This matches the WebRTC observer pattern.
 */
typedef struct {
    /**
     * @brief Called when a new ICE candidate has been gathered locally
     *
     * If using manual signaling, the application should transmit this candidate
     * to the remote peer through its signaling channel. If using built-in
     * signaling, this is done automatically.
     */
    void (*on_ice_candidate)(void *user_data, tinyrtc_ice_candidate_t *candidate);

    /**
     * @brief Called when ICE candidate gathering is complete
     */
    void (*on_ice_gathering_complete)(void *user_data);

    /**
     * @brief Called when the connection state changes
     *
     * This is the primary way to track connection progress.
     */
    void (*on_connection_state_change)(void *user_data, tinyrtc_pc_state_t new_state);

    /**
     * @brief Called when a remote media track has been added
     *
     * Application should start receiving media from this track.
     */
    void (*on_track_added)(void *user_data, tinyrtc_track_t *track);

    /**
     * @brief Called when a remote media track has been removed
     */
    void (*on_track_removed)(void *user_data, tinyrtc_track_t *track);

    /**
     * @brief Called when a complete encoded audio frame has been received
     *
     * TinyRTC handles packetization/depacketization internally.
     * Application gets complete encoded frames ready for decoding.
     */
    void (*on_audio_frame)(void *user_data, tinyrtc_track_t *track,
                           const uint8_t *frame, size_t frame_len,
                           uint32_t timestamp);

    /**
     * @brief Called when a complete encoded video frame has been received
     *
     * TinyRTC handles packetization/depacketization internally.
     * Application gets complete encoded frames ready for decoding.
     */
    void (*on_video_frame)(void *user_data, tinyrtc_track_t *track,
                           const uint8_t *frame, size_t frame_len,
                           uint32_t timestamp);

    /**
     * @brief Called when a data channel message is received
     *
     * Only available if data channel is enabled.
     */
    void (*on_data_message)(void *user_data, const uint8_t *data, size_t len);

    /**
     * @brief User context passed to all callbacks
     */
    void *user_data;
} tinyrtc_pc_observer_t;

/**
 * @brief PeerConnection configuration
 */
typedef struct {
    /**
     * @brief STUN server URL (e.g., "stun:stun.l.google.com:19302")
     *
     * Required for NAT traversal when connecting through the internet.
     */
    const char *stun_server;

    /**
     * @brief TURN server URL (optional) for fallback when P2P fails
     * Format: "turn:turn.example.com:3478?transport=udp"
     */
    const char *turn_server;

    /**
     * @brief TURN server username (if required)
     */
    const char *turn_username;

    /**
     * @brief TURN server password (if required)
     */
    const char *turn_password;

    /**
     * @brief Event observer callbacks
     */
    tinyrtc_pc_observer_t observer;

    /**
     * @brief Whether this peer is the initiator (makes the initial offer)
     */
    bool is_initiator;

    /**
     * @brief Enable data channel (optional, not implemented yet)
     */
    bool enable_data_channel;
} tinyrtc_pc_config_t;

/**
 * @brief PeerConnection handle - represents one WebRTC connection to a remote peer
 */
typedef struct tinyrtc_peer_connection tinyrtc_peer_connection_t;

/**
 * @brief Create a new PeerConnection
 *
 * @param ctx TinyRTC global context
 * @param config Connection configuration
 * @return New PeerConnection handle, NULL on error
 */
tinyrtc_peer_connection_t *tinyrtc_peer_connection_create(
    tinyrtc_context_t *ctx,
    const tinyrtc_pc_config_t *config);

/**
 * @brief Destroy a PeerConnection
 *
 * Closes the connection if open and frees all resources.
 *
 * @param pc PeerConnection to destroy
 */
void tinyrtc_peer_connection_destroy(tinyrtc_peer_connection_t *pc);

/**
 * @brief Get current connection state
 *
 * @param pc PeerConnection
 * @return Current connection state
 */
tinyrtc_pc_state_t tinyrtc_peer_connection_get_state(tinyrtc_peer_connection_t *pc);

/**
 * @brief Create an SDP offer to send to the remote peer
 *
 * Call this when starting a new connection as the initiator.
 * The generated SDP must be sent to the remote peer via your signaling channel.
 *
 * @param pc PeerConnection
 * @param[out] out_sdp Allocated SDP string (must be freed by caller using tinyrtc_free())
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_peer_connection_create_offer(
    tinyrtc_peer_connection_t *pc,
    char **out_sdp);

/**
 * @brief Set the remote SDP description (offer or answer)
 *
 * Call this when you receive an SDP from the remote peer via your signaling channel.
 *
 * @param pc PeerConnection
 * @param sdp Remote SDP description
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_peer_connection_set_remote_description(
    tinyrtc_peer_connection_t *pc,
    const char *sdp);

/**
 * @brief Create an SDP answer in response to a remote offer
 *
 * Call this after setting a remote offer to create your answer.
 * The answer must be sent back to the remote peer via your signaling channel.
 *
 * @param pc PeerConnection
 * @param[out] out_sdp Allocated SDP string (must be freed by caller)
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_peer_connection_create_answer(
    tinyrtc_peer_connection_t *pc,
    char **out_sdp);

/**
 * @brief Add a remote ICE candidate received through signaling
 *
 * Called as remote candidates arrive from the signaling channel.
 *
 * @param pc PeerConnection
 * @param candidate Remote ICE candidate
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_peer_connection_add_ice_candidate(
    tinyrtc_peer_connection_t *pc,
    tinyrtc_ice_candidate_t *candidate);

/**
 * @brief Add a local media track to be sent to the remote peer
 *
 * Add audio or video tracks before creating an offer/answer.
 * After connection is established, use tinyrtc_track_send_frame() to send data.
 *
 * @param pc PeerConnection
 * @param config Track configuration
 * @return New track handle, NULL on error
 */
tinyrtc_track_t *tinyrtc_peer_connection_add_track(
    tinyrtc_peer_connection_t *pc,
    const tinyrtc_track_config_t *config);

/**
 * @brief Remove a local media track
 *
 * @param pc PeerConnection
 * @param track Track to remove
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_peer_connection_remove_track(
    tinyrtc_peer_connection_t *pc,
    tinyrtc_track_t *track);

/**
 * @brief Close the PeerConnection
 *
 * Closes all network connections and transitions to CLOSED state.
 *
 * @param pc PeerConnection to close
 */
void tinyrtc_peer_connection_close(tinyrtc_peer_connection_t *pc);

/* =============================================================================
 * Track API - Send media frames
 * ========================================================================== */

/**
 * @brief Send an encoded audio frame to the remote peer
 *
 * This is the recommended API. TinyRTC handles packetization into RTP
 * internally based on the configured codec.
 *
 * Application provides the encoded frame, TinyRTC does the rest.
 * TinyRTC does NOT include encoder - encoding is provided by application
 * or platform device layer.
 *
 * @param track Media track
 * @param frame Complete encoded audio frame
 * @param frame_len Frame length in bytes
 * @param timestamp RTP timestamp for this frame
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_track_send_audio_frame(
    tinyrtc_track_t *track,
    const uint8_t *frame,
    size_t frame_len,
    uint32_t timestamp);

/**
 * @brief Send an encoded video frame to the remote peer
 *
 * This is the recommended API. TinyRTC handles packetization into RTP
 * internally based on the configured codec.
 *
 * Application provides the encoded frame, TinyRTC does the rest.
 * TinyRTC does NOT include encoder - encoding is provided by application
 * or platform device layer.
 *
 * @param track Media track
 * @param frame Complete encoded video frame
 * @param frame_len Frame length in bytes
 * @param timestamp RTP timestamp for this frame
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_track_send_video_frame(
    tinyrtc_track_t *track,
    const uint8_t *frame,
    size_t frame_len,
    uint32_t timestamp);

/**
 * @brief Get track kind (audio or video)
 *
 * @param track Media track
 * @return Track kind
 */
tinyrtc_track_kind_t tinyrtc_track_get_kind(tinyrtc_track_t *track);

/**
 * @brief Get track codec ID
 *
 * @param track Media track
 * @return Codec ID
 */
tinyrtc_codec_id_t tinyrtc_track_get_codec(tinyrtc_track_t *track);

/**
 * @brief Get track MID (media identifier)
 *
 * @param track Media track
 * @return MID string (valid as long as track exists)
 */
const char *tinyrtc_track_get_mid(tinyrtc_track_t *track);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_PEER_CONNECTION_H */
