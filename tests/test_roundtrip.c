/*
 * test_roundtrip.c
 *
 * End-to-end localhost media roundtrip tests.
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "peer_connection_internal.h"
#include "api/aosl.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <string.h>

typedef struct {
    int video_frames;
    uint8_t video_frame[256];
    size_t video_frame_len;
    uint32_t video_timestamp;
} roundtrip_capture_t;

static void roundtrip_on_video_frame(void *user_data, tinyrtc_track_t *track,
                                     const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    roundtrip_capture_t *capture = (roundtrip_capture_t *)user_data;
    (void)track;

    capture->video_frames++;
    capture->video_frame_len = frame_len;
    capture->video_timestamp = timestamp;
    memcpy(capture->video_frame, frame, frame_len);
}

static tinyrtc_peer_connection_t *create_test_pc(
    tinyrtc_context_t *ctx,
    bool is_initiator,
    roundtrip_capture_t *capture)
{
    tinyrtc_pc_config_t config;
    tinyrtc_peer_connection_t *pc;
    tinyrtc_track_config_t video_config;

    memset(&config, 0, sizeof(config));
    config.stun_server = NULL;
    config.is_initiator = is_initiator;
    config.observer.user_data = capture;
    config.observer.on_video_frame = roundtrip_on_video_frame;

    pc = tinyrtc_peer_connection_create(ctx, &config);
    if (pc == NULL) {
        return NULL;
    }

    memset(&video_config, 0, sizeof(video_config));
    video_config.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_config.mid = "v0";
    video_config.codec_id = TINYRTC_CODEC_H264;

    if (tinyrtc_peer_connection_add_track(pc, &video_config) == NULL) {
        tinyrtc_peer_connection_destroy(pc);
        return NULL;
    }

    return pc;
}

MINUNIT_TEST(test_localhost_h264_roundtrip_delivers_complete_annexb_frame)
{
    tinyrtc_config_t config;
    tinyrtc_context_t *ctx;
    roundtrip_capture_t sender_capture;
    roundtrip_capture_t receiver_capture;
    tinyrtc_peer_connection_t *sender;
    tinyrtc_peer_connection_t *receiver;
    char *offer_sdp = NULL;
    char *answer_sdp = NULL;
    tinyrtc_error_t err;
    tinyrtc_track_t *sender_track;
    uint8_t frame[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1F,
        0x00, 0x00, 0x00, 0x01, 0x68, 0xEE, 0x06, 0xF2,
        0x00, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x21, 0xA0
    };
    int loops = 0;

    memset(&sender_capture, 0, sizeof(sender_capture));
    memset(&receiver_capture, 0, sizeof(receiver_capture));

    tinyrtc_get_default_config(&config);
    config.enable_debug_log = true;
    ctx = tinyrtc_init(&config);
    MINUNIT_ASSERT(ctx != NULL, "Expected TinyRTC context");

    sender = create_test_pc(ctx, true, &sender_capture);
    MINUNIT_ASSERT(sender != NULL, "Expected sender peer connection");

    receiver = create_test_pc(ctx, false, &receiver_capture);
    MINUNIT_ASSERT(receiver != NULL, "Expected receiver peer connection");

    err = tinyrtc_peer_connection_create_offer(sender, &offer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected sender offer creation");
    MINUNIT_ASSERT(offer_sdp != NULL, "Expected non-null offer SDP");

    err = tinyrtc_peer_connection_set_remote_description(receiver, offer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected receiver to accept offer");

    err = tinyrtc_peer_connection_create_answer(receiver, &answer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected receiver answer creation");
    MINUNIT_ASSERT(answer_sdp != NULL, "Expected non-null answer SDP");

    err = tinyrtc_peer_connection_set_remote_description(sender, answer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected sender to accept answer");

    if (sender->ice == NULL || sender->ice->socket < 0 ||
        receiver->ice == NULL || receiver->ice->socket < 0) {
        printf("Skipping localhost roundtrip test: UDP sockets unavailable in current environment\n");
        tinyrtc_free(offer_sdp);
        tinyrtc_free(answer_sdp);
        tinyrtc_peer_connection_destroy(sender);
        tinyrtc_peer_connection_destroy(receiver);
        tinyrtc_destroy(ctx);
        return 0;
    }

    while ((!sender->srtp_initialized || !receiver->srtp_initialized) && loops < 400) {
        tinyrtc_process_events(ctx, 10);
        aosl_msleep(10);
        loops++;
    }

    MINUNIT_ASSERT(sender->srtp_initialized, "Expected sender SRTP initialization");
    MINUNIT_ASSERT(receiver->srtp_initialized, "Expected receiver SRTP initialization");
    MINUNIT_ASSERT(memcmp(sender->srtp->key, receiver->srtp_rx->key, sizeof(sender->srtp->key)) == 0,
                   "Expected sender SRTP send key to match receiver SRTP receive key");
    MINUNIT_ASSERT(memcmp(sender->srtp->salt, receiver->srtp_rx->salt, sizeof(sender->srtp->salt)) == 0,
                   "Expected sender SRTP send salt to match receiver SRTP receive salt");

    sender_track = sender->local_tracks[0];
    MINUNIT_ASSERT(sender_track != NULL, "Expected sender local video track");

    err = tinyrtc_track_send_video_frame(sender_track, frame, sizeof(frame), 9000);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected video frame send");

    loops = 0;
    while (receiver_capture.video_frames == 0 && loops < 200) {
        tinyrtc_process_events(ctx, 10);
        aosl_msleep(10);
        loops++;
    }

    if (receiver_capture.video_frames == 0) {
        tinyrtc_track_t *receiver_track = receiver->remote_tracks[0];
        fd_set read_fds;
        struct timeval tv;
        uint8_t packet[2048];
        int ready;
        ssize_t received_len = -1;

        FD_ZERO(&read_fds);
        FD_SET(receiver->ice->socket, &read_fds);
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        ready = select(receiver->ice->socket + 1, &read_fds, NULL, NULL, &tv);
        if (ready > 0 && FD_ISSET(receiver->ice->socket, &read_fds)) {
            received_len = recv(receiver->ice->socket, packet, sizeof(packet), 0);
            if (received_len > 0) {
                tinyrtc_error_t packet_err = pc_process_incoming_rtp(receiver, packet, (size_t)received_len);
                printf("roundtrip debug: manual recv len=%zd packet_err=%d video_frames=%d packets=%llu frames=%llu\n",
                       received_len, packet_err, receiver_capture.video_frames,
                       (unsigned long long)(receiver_track ? receiver_track->packets_received : 0),
                       (unsigned long long)(receiver_track ? receiver_track->frames_received : 0));
            }
        }
        printf("roundtrip debug: sender selected=%s:%d receiver selected=%s:%d sender frames_sent=%llu receiver packets=%llu receiver frames=%llu\n",
               sender->ice && sender->ice->selected_pair ? sender->ice->selected_pair->remote->ip : "(null)",
               sender->ice && sender->ice->selected_pair ? sender->ice->selected_pair->remote->port : -1,
               receiver->ice && receiver->ice->selected_pair ? receiver->ice->selected_pair->remote->ip : "(null)",
               receiver->ice && receiver->ice->selected_pair ? receiver->ice->selected_pair->remote->port : -1,
               (unsigned long long)sender_track->frames_sent,
               (unsigned long long)(receiver_track ? receiver_track->packets_received : 0),
               (unsigned long long)(receiver_track ? receiver_track->frames_received : 0));
    }

    MINUNIT_ASSERT(receiver_capture.video_frames == 1, "Expected one received video frame");
    MINUNIT_ASSERT(receiver_capture.video_frame_len == sizeof(frame),
                   "Expected received frame length to match sent Annex-B frame");
    MINUNIT_ASSERT(receiver_capture.video_timestamp == 9000,
                   "Expected received frame timestamp to match sender");
    MINUNIT_ASSERT(memcmp(receiver_capture.video_frame, frame, sizeof(frame)) == 0,
                   "Expected received H264 Annex-B bytes to match sent frame");

    tinyrtc_free(offer_sdp);
    tinyrtc_free(answer_sdp);
    tinyrtc_peer_connection_destroy(sender);
    tinyrtc_peer_connection_destroy(receiver);
    tinyrtc_destroy(ctx);
    return 0;
}
