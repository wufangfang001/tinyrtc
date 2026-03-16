/**
 * @file demo_sender.c
 * @brief Demo: WebRTC sender that sends video to browser
 *
 * Usage:
 * 1. Run ./tinyrtc_send to generate offer
 * 2. Copy generated offer.sdp to browser test page
 * 3. Copy browser answer to answer.sdp
 * 4. Run ./tinyrtc_send --with-answer answer.sdp
 *
 * This demo doesn't include actual video capture/encoding - you need
 * to provide encoded frames from your camera/device.
 */

#include "common.h"
#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/peer_connection.h"
#include "api/aosl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_ice_candidate(void *user_data, tinyrtc_ice_candidate_t *candidate)
{
    aosl_log(AOSL_LOG_INFO, "Got local ICE candidate: %s:%d type=%s", candidate->ip, candidate->port, candidate->type);
}

static void on_connection_state_change(void *user_data, tinyrtc_pc_state_t new_state)
{
    static const char *state_names[] = {
        "NEW", "CONNECTING", "CONNECTED", "DISCONNECTED", "FAILED", "CLOSED"
    };
    aosl_log(AOSL_LOG_INFO, "Connection state changed: %s", state_names[new_state]);
}

static void on_track_added(void *user_data, tinyrtc_track_t *track)
{
    const char *kind = tinyrtc_track_get_kind(track) == TINYRTC_TRACK_KIND_AUDIO ? "audio" : "video";
    const char *codec = tinyrtc_codec_get_name(tinyrtc_track_get_codec(track));
    aosl_log(AOSL_LOG_INFO, "Remote track added: %s codec=%s mid=%s", kind, codec, tinyrtc_track_get_mid(track));
}

static void on_audio_frame(void *user_data, tinyrtc_track_t *track,
                            const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    /* In a real application, you would decode and play this audio frame here.
     * TinyRTC handles packetization/depacketization internally - you get complete
     * encoded frames ready for decoding. */
    (void)user_data;
    (void)track;
    (void)frame;
    (void)frame_len;
    (void)timestamp;
}

static void on_video_frame(void *user_data, tinyrtc_track_t *track,
                           const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    /* In a real application, you would decode and display/render this video frame here.
     * TinyRTC handles packetization/depacketization internally - you get complete
     * encoded frames ready for decoding. */
    (void)user_data;
    (void)track;
    (void)frame;
    (void)frame_len;
    (void)timestamp;
}

int main(int argc, char **argv)
{
    demo_init_aosl();

    /* Initialize TinyRTC */
    tinyrtc_config_t config;
    tinyrtc_get_default_config(&config);
    tinyrtc_context_t *ctx = tinyrtc_init(&config);
    if (!ctx) {
        aosl_log(AOSL_LOG_ERROR, "Failed to initialize TinyRTC");
        return 1;
    }

    /* Configure peer connection */
    tinyrtc_pc_config_t pc_config = {0};
    pc_config.stun_server = "stun:stun.l.google.com:19302";
    pc_config.observer.on_ice_candidate = on_ice_candidate;
    pc_config.observer.on_connection_state_change = on_connection_state_change;
    pc_config.observer.on_track_added = on_track_added;
    pc_config.observer.on_audio_frame = on_audio_frame;
    pc_config.observer.on_video_frame = on_video_frame;
    pc_config.is_initiator = true;

    tinyrtc_peer_connection_t *pc = tinyrtc_peer_connection_create(ctx, &pc_config);
    if (!pc) {
        aosl_log(AOSL_LOG_ERROR, "Failed to create peer connection");
        tinyrtc_destroy(ctx);
        return 1;
    }

    /* Add video track (we send video) */
    tinyrtc_track_config_t video_config = {0};
    video_config.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_config.mid = "v0";
    video_config.codec_id = TINYRTC_CODEC_H264;
    /* payload_type and clock_rate auto-filled from codec defaults */
    tinyrtc_track_t *video_track = tinyrtc_peer_connection_add_track(pc, &video_config);

    /* Add audio track (we send audio) */
    tinyrtc_track_config_t audio_config = {0};
    audio_config.kind = TINYRTC_TRACK_KIND_AUDIO;
    audio_config.mid = "a0";
    audio_config.codec_id = TINYRTC_CODEC_OPUS;
    /* payload_type and clock_rate auto-filled from codec defaults */
    tinyrtc_peer_connection_add_track(pc, &audio_config);

    /* Check if we have an answer from previous step */
    if (argc == 3 && strcmp(argv[1], "--with-answer") == 0) {
        /* Read answer from file */
        char *answer_sdp = demo_read_sdp(argv[2]);
        if (!answer_sdp) {
            aosl_log(AOSL_LOG_ERROR, "Failed to read answer from %s", argv[2]);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Setting remote description from answer...");
        tinyrtc_error_t err = tinyrtc_peer_connection_set_remote_description(pc, answer_sdp);
        tinyrtc_free(answer_sdp);

        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to set remote description: %s", tinyrtc_get_error_string(err));
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)");
        aosl_log(AOSL_LOG_INFO, "In a real application, you would read encoded frames from camera,");
        aosl_log(AOSL_LOG_INFO, "and call tinyrtc_track_send_video_frame() / tinyrtc_track_send_audio_frame() to send.");

        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            tinyrtc_process_events(ctx, 100);
            aosl_msleep(10);
        }
    } else {
        /* Create offer and write to file */
        char *offer_sdp = NULL;
        tinyrtc_error_t err = tinyrtc_peer_connection_create_offer(pc, &offer_sdp);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create offer: %s", tinyrtc_get_error_string(err));
            goto cleanup;
        }

        int wr = demo_write_sdp("offer.sdp", offer_sdp);
        if (wr != 0) {
            aosl_log(AOSL_LOG_ERROR, "Failed to write offer to offer.sdp");
            tinyrtc_free(offer_sdp);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Offer generated and saved to offer.sdp");
        aosl_log(AOSL_LOG_INFO, "Next step:");
        aosl_log(AOSL_LOG_INFO, "  1. Open tools/browser_test.html in your browser");
        aosl_log(AOSL_LOG_INFO, "  2. Paste offer.sdp content into browser");
        aosl_log(AOSL_LOG_INFO, "  3. Copy the generated answer from browser to answer.sdp");
        aosl_log(AOSL_LOG_INFO, "  4. Run: %s --with-answer answer.sdp", argv[0]);
        tinyrtc_free(offer_sdp);
    }

cleanup:
    tinyrtc_peer_connection_destroy(pc);
    tinyrtc_destroy(ctx);
    demo_exit_aosl();
    return 0;
}
