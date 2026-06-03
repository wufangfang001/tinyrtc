/*
 * test_peer_connection.c
 *
 * Unit tests for internal PeerConnection media send readiness.
 */

#include "minunit.h"
#include "peer_connection_internal.h"

typedef struct {
    int calls;
    uint8_t frame[64];
    size_t frame_len;
    uint32_t timestamp;
} video_capture_t;

static void capture_video_frame(void *user_data, tinyrtc_track_t *track,
                                const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    video_capture_t *capture = (video_capture_t *)user_data;
    (void)track;

    capture->calls++;
    capture->frame_len = frame_len;
    capture->timestamp = timestamp;
    memcpy(capture->frame, frame, frame_len);
}

MINUNIT_TEST(test_pc_secure_media_ready_requires_srtp)
{
    tinyrtc_peer_connection_t pc;
    ice_session_t ice;
    ice_check_pair_t pair;
    ice_candidate_internal_t local;
    ice_candidate_internal_t remote;

    memset(&pc, 0, sizeof(pc));
    memset(&ice, 0, sizeof(ice));
    memset(&pair, 0, sizeof(pair));
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    pair.local = &local;
    pair.remote = &remote;
    pair.succeeded = true;

    ice.socket = 7;
    ice.selected_pair = &pair;
    pc.ice = &ice;

    MINUNIT_ASSERT(!pc_is_secure_media_ready(&pc),
                   "Expected media send to stay blocked before SRTP is initialized");

    pc.srtp_initialized = true;

    MINUNIT_ASSERT(pc_is_secure_media_ready(&pc),
                   "Expected media send to become ready after SRTP initialization");

    return 0;
}

MINUNIT_TEST(test_pc_secure_media_ready_rejects_missing_dtls_master_secret)
{
    tinyrtc_peer_connection_t pc;
    dtls_context_t *dtls;
    uint8_t client_key[16];
    uint8_t client_salt[14];
    uint8_t server_key[16];
    uint8_t server_salt[14];
    tinyrtc_error_t err;
    int old_log_level;

    memset(&pc, 0, sizeof(pc));

    dtls = dtls_init(DTLS_ROLE_CLIENT);
    MINUNIT_ASSERT(dtls != NULL, "Expected DTLS init to succeed");
    dtls->handshake_complete = true;
    if (dtls->ssl != NULL) {
        dtls->ssl->state = MBEDTLS_SSL_HANDSHAKE_OVER;
    }
    pc.dtls = dtls;

    old_log_level = aosl_get_log_level();
    aosl_set_log_level(AOSL_LOG_CRIT);
    err = dtls_derive_srtp_keys(dtls, client_key, client_salt, server_key, server_salt);
    aosl_set_log_level(old_log_level);
    MINUNIT_ASSERT(err == TINYRTC_ERROR_INVALID_STATE,
                   "Expected SRTP key derivation to fail when master secret was not captured");
    MINUNIT_ASSERT(!pc.srtp_initialized,
                   "Expected peer connection SRTP state to remain false after derivation failure");
    MINUNIT_ASSERT(pc.srtp == NULL,
                   "Expected peer connection SRTP context to remain NULL after derivation failure");

    dtls_destroy(dtls);
    return 0;
}

MINUNIT_TEST(test_pc_process_incoming_rtp_reassembles_h264_fua_before_callback)
{
    tinyrtc_peer_connection_t pc;
    tinyrtc_track_t track;
    tinyrtc_jitter_config_t jb_config;
    video_capture_t capture;
    uint8_t packet1[] = {
        0x80, 0x64, 0x00, 0x01,
        0x00, 0x00, 0x03, 0xE8,
        0x12, 0x34, 0x56, 0x78,
        0x7C, 0x85,
        0x11, 0x22, 0x33
    };
    uint8_t packet2[] = {
        0x80, 0xE4, 0x00, 0x02,
        0x00, 0x00, 0x03, 0xE8,
        0x12, 0x34, 0x56, 0x78,
        0x7C, 0x45,
        0x44, 0x55, 0x66
    };
    const uint8_t expected[] = {
        0x00, 0x00, 0x00, 0x01,
        0x65, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66
    };
    tinyrtc_error_t err;

    memset(&pc, 0, sizeof(pc));
    memset(&track, 0, sizeof(track));
    memset(&capture, 0, sizeof(capture));

    pc.mutex = aosl_lock_create();
    MINUNIT_ASSERT(pc.mutex != NULL, "Expected peer connection mutex");
    pc.config.observer.user_data = &capture;
    pc.config.observer.on_video_frame = capture_video_frame;

    tinyrtc_jitter_get_default_config(&jb_config);
    track.jitter_buffer = tinyrtc_jitter_buffer_create(&jb_config);
    MINUNIT_ASSERT(track.jitter_buffer != NULL, "Expected remote track jitter buffer");
    track.kind = TINYRTC_TRACK_KIND_VIDEO;
    track.codec_id = TINYRTC_CODEC_H264;
    track.payload_type = 100;
    track.pc = &pc;

    pc.remote_tracks[0] = &track;
    pc.num_remote_tracks = 1;

    err = pc_process_incoming_rtp(&pc, packet1, sizeof(packet1));
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected first H264 fragment to be accepted");
    MINUNIT_ASSERT(capture.calls == 0, "Expected no callback before marker packet");

    err = pc_process_incoming_rtp(&pc, packet2, sizeof(packet2));
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected final H264 fragment to be accepted");
    MINUNIT_ASSERT(capture.calls == 1, "Expected single callback after full H264 frame reassembly");
    MINUNIT_ASSERT(capture.frame_len == sizeof(expected), "Expected reassembled frame length");
    MINUNIT_ASSERT(capture.timestamp == 1000, "Expected callback timestamp from RTP header");
    MINUNIT_ASSERT(memcmp(capture.frame, expected, sizeof(expected)) == 0,
                   "Expected callback frame to match rebuilt Annex-B data");

    tinyrtc_jitter_buffer_destroy(track.jitter_buffer);
    aosl_lock_destroy(pc.mutex);

    return 0;
}

MINUNIT_TEST(test_track_send_video_frame_rejects_when_secure_media_not_ready)
{
    tinyrtc_track_t track;
    uint8_t frame[] = { 0x00, 0x00, 0x00, 0x01, 0x65, 0x88 };
    tinyrtc_error_t err;

    memset(&track, 0, sizeof(track));
    track.is_local = true;
    track.codec_id = TINYRTC_CODEC_H264;
    track.payload_type = 100;

    err = tinyrtc_track_send_video_frame(&track, frame, sizeof(frame), 9000);
    MINUNIT_ASSERT(err == TINYRTC_ERROR_INVALID_STATE,
                   "Expected video send to fail before secure media is ready");

    return 0;
}
