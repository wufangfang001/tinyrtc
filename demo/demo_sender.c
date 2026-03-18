/**
 * @file demo_sender.c
 * @brief Demo: WebRTC sender that sends video to browser
 *
 * Usage with automatic signaling:
 *   ./tinyrtc_send --room <room-id>
 *   Then open browser_test.html with same room-id and it will connect automatically
 *
 * Usage with manual signaling (original):
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
#include "tinyrtc/signaling.h"
#include "api/aosl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_ice_candidate(void *user_data, tinyrtc_ice_candidate_t *candidate) {
    aosl_log(AOSL_LOG_INFO, "Got local ICE candidate: %s:%d type=%s",
            candidate->ip, candidate->port, candidate->type);
}

static void on_connection_state_change(void *user_data, tinyrtc_pc_state_t new_state) {
    static const char *state_names[] = {
        "NEW", "CONNECTING", "CONNECTED", "DISCONNECTED", "FAILED", "CLOSED"
    };
    aosl_log(AOSL_LOG_INFO, "Connection state changed: %s", state_names[new_state]);
}

static void on_track_added(void *user_data, tinyrtc_track_t *track) {
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

/* Global for signaling callback */
static tinyrtc_peer_connection_t *g_pc = NULL;
static bool g_got_answer = false;

static void signaling_callback(tinyrtc_signal_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case TINYRTC_SIGNAL_EVENT_ANSWER:
            aosl_log(AOSL_LOG_INFO, "Received answer from signaling server");
            if (g_pc && event->data.answer) {
                tinyrtc_error_t err = tinyrtc_peer_connection_set_remote_description(g_pc, event->data.answer);
                if (err != TINYRTC_OK) {
                    aosl_log(AOSL_LOG_ERROR, "Failed to set remote answer: %s",
                            tinyrtc_get_error_string(err));
                } else {
                    aosl_log(AOSL_LOG_INFO, "Remote answer set successfully");
                    g_got_answer = true;
                }
                aosl_free(event->data.answer);
            }
            break;
        case TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE:
            aosl_log(AOSL_LOG_DEBUG, "Received ICE candidate from signaling");
            /* TODO: add ICE candidate API to TinyRTC */
            break;
        default:
            break;
    }
}

int main(int argc, char **argv)
{
    const char *room_id = "tinyrtc-demo";
    bool auto_signaling = false;
    const char *default_signaling_server = "ws://localhost:8080";

    /* Parse arguments */
    if (argc >= 3 && strcmp(argv[1], "--room") == 0) {
        room_id = argv[2];
        auto_signaling = true;
    }
    /* Usage: ./tinyrtc_send --room room-id --server ws://your-server-ip:8080 */
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--server") == 0) {
            default_signaling_server = argv[i+1];
        }
    }

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

    g_pc = pc;

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

    /* Check for manual mode with answer from previous step */
    if (!auto_signaling && argc == 3 && strcmp(argv[1], "--with-answer") == 0) {
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
    } else if (auto_signaling) {
        /* Automatic signaling using public signaling server */
        aosl_log(AOSL_LOG_INFO, "Starting automatic signaling... room=%s server=%s",
                room_id, default_signaling_server);

        aosl_log(AOSL_LOG_INFO, "Starting signaling connect to %s room=%s",
                default_signaling_server, room_id);

        tinyrtc_signaling_config_t sig_config = {0};
        sig_config.url = (char *)default_signaling_server;
        sig_config.room_id = (char *)room_id;
        sig_config.client_id = NULL; /* auto-generate */
        sig_config.auto_connect = true;

        aosl_log(AOSL_LOG_INFO, "Calling tinyrtc_signaling_create...");
        tinyrtc_signaling_t *sig = tinyrtc_signaling_create(
            ctx, &sig_config, signaling_callback, NULL);

        if (!sig) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create signaling client");
            goto cleanup;
        }

        tinyrtc_error_t err = tinyrtc_signaling_connect(sig);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to connect to signaling server: %s",
                    tinyrtc_get_error_string(err));
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Connected to signaling server successfully");

        /* Create offer and send via signaling */
        char *offer_sdp = NULL;
        err = tinyrtc_peer_connection_create_offer(pc, &offer_sdp);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create offer: %s", tinyrtc_get_error_string(err));
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Created offer, sending to signaling server...");
        err = tinyrtc_signaling_send_offer(sig, NULL, offer_sdp);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to send offer: %s",
                    tinyrtc_get_error_string(err));
            tinyrtc_free(offer_sdp);
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Offer sent. Waiting for answer from browser...");
        aosl_log(AOSL_LOG_INFO, "Open browser_test.html and enter room-id: %s", room_id);
        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)");

        /* Main loop: poll signaling and process TinyRTC events */
        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            /* Process signaling messages */
            /* Note: signaling doesn't need explicit poll, because we use blocking
             * read which will be processed in this loop */
            tinyrtc_process_events(ctx, 100);
            aosl_msleep(10);
        }

        tinyrtc_free(offer_sdp);
        tinyrtc_signaling_destroy(sig);
    } else {
        /* Manual mode: Create offer and write to file */
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
        aosl_log(AOSL_LOG_INFO, "Next step (manual mode):");
        aosl_log(AOSL_LOG_INFO, "  1. Open tools/browser_test.html in your browser");
        aosl_log(AOSL_LOG_INFO, "  2. Paste offer.sdp content into browser");
        aosl_log(AOSL_LOG_INFO, "  3. Copy the generated answer from browser to answer.sdp");
        aosl_log(AOSL_LOG_INFO, "  4. Run: %s --with-answer answer.sdp", argv[0]);
        aosl_log(AOSL_LOG_INFO, "");
        aosl_log(AOSL_LOG_INFO, "Or use automatic mode:");
        aosl_log(AOSL_LOG_INFO, "  %s --room %s  (connects via public signaling server)", argv[0], room_id);
        tinyrtc_free(offer_sdp);
    }

cleanup:
    g_pc = NULL;
    tinyrtc_peer_connection_destroy(pc);
    tinyrtc_destroy(ctx);
    demo_exit_aosl();
    return 0;
}
