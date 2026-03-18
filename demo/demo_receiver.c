/**
 * @file demo_receiver.c
 * @brief Demo: WebRTC receiver that receives video from browser
 *
 * Usage with automatic signaling:
 *   ./tinyrtc_recv --room <room-id>
 *   Then open browser_test.html with same room-id and it will connect automatically
 *
 * Usage with manual signaling (original):
 * 1. Get offer from browser test page, save as offer.sdp
 * 2. Run ./tinyrtc_recv --offer offer.sdp to generate answer.sdp
 * 3. Copy answer.sdp back to browser to complete connection
 * 4. Start receiving media frames from browser
 *
 * This demo doesn't include actual decoding/rendering - you need
 * to provide that using platform-specific device capabilities.
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
    aosl_log(AOSL_LOG_INFO, "Got local ICE candidate: %s:%d type=%s\n", candidate->ip, candidate->port, candidate->type);
}

static void on_connection_state_change(void *user_data, tinyrtc_pc_state_t new_state) {
    static const char *state_names[] = {
        "NEW", "CONNECTING", "CONNECTED", "DISCONNECTED", "FAILED", "CLOSED"
    };
    aosl_log(AOSL_LOG_INFO, "Connection state changed: %s\n", state_names[new_state]);
}

static void on_track_added(void *user_data, tinyrtc_track_t *track) {
    const char *kind = tinyrtc_track_get_kind(track) == TINYRTC_TRACK_KIND_AUDIO ? "audio" : "video";
    const char *codec = tinyrtc_codec_get_name(tinyrtc_track_get_codec(track));
    aosl_log(AOSL_LOG_INFO, "Remote track added: %s codec=%s mid=%s\n", kind, codec, tinyrtc_track_get_mid(track));
}

static int g_audio_frame_count = 0;
static int g_video_frame_count = 0;
static FILE *g_audio_file = NULL;
static FILE *g_video_file = NULL;

static void on_audio_frame(void *user_data, tinyrtc_track_t *track,
                            const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    /* In a real application, you would decode and play this frame here.
     * TinyRTC handles packetization/depacketization internally - you get complete
     * encoded frames ready for decoding. */
    (void)user_data;
    (void)track;
    (void)timestamp;

    /* Save the encoded frame to file */
    if (g_audio_file && frame_len > 0) {
        fwrite(frame, 1, frame_len, g_audio_file);
        fflush(g_audio_file);
    }

    g_audio_frame_count++;
    if (g_audio_frame_count % 100 == 1) {
        aosl_log(AOSL_LOG_INFO, "Received audio frame #%d, size=%zu bytes, ts=%u, saved to file\n",
                g_audio_frame_count, frame_len, timestamp);
    }
}

static void on_video_frame(void *user_data, tinyrtc_track_t *track,
                           const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    /* In a real application, you would decode and display/render this video frame here.
     * TinyRTC handles packetization/depacketization internally - you get complete
     * encoded frames ready for decoding. */
    (void)user_data;
    (void)track;
    (void)timestamp;

    /* Save the encoded frame to file */
    if (g_video_file && frame_len > 0) {
        fwrite(frame, 1, frame_len, g_video_file);
        fflush(g_video_file);
    }

    g_video_frame_count++;
    if (g_video_frame_count % 30 == 1) {
        aosl_log(AOSL_LOG_INFO, "Received video frame #%d, size=%zu bytes, ts=%u, saved to file\n",
                g_video_frame_count, frame_len, timestamp);
    }
}

/* Global for signaling callback */
static tinyrtc_peer_connection_t *g_pc = NULL;
static bool g_got_offer = false;
static char *g_pending_offer = NULL;

static void signaling_callback(tinyrtc_signal_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case TINYRTC_SIGNAL_EVENT_OFFER:
            aosl_log(AOSL_LOG_INFO, "Received offer from signaling server\n");
            if (g_pc && event->data.offer) {
                g_pending_offer = aosl_malloc(strlen(event->data.offer) + 1);
                if (g_pending_offer) {
                    strcpy(g_pending_offer, event->data.offer);
                }
                g_got_offer = true;
                aosl_free(event->data.offer);
            }
            break;
        case TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE:
            aosl_log(AOSL_LOG_DEBUG, "Received ICE candidate from signaling\n");
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
    /* Usage: ./tinyrtc_recv --room room-id --server ws://your-server-ip:8080 */
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
        aosl_log(AOSL_LOG_ERROR, "Failed to initialize TinyRTC\n");
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
    pc_config.is_initiator = false;

    tinyrtc_peer_connection_t *pc = tinyrtc_peer_connection_create(ctx, &pc_config);
    if (!pc) {
        aosl_log(AOSL_LOG_ERROR, "Failed to create peer connection\n");
        tinyrtc_destroy(ctx);
        return 1;
    }

    g_pc = pc;

    /* Open files to save received frames */
    g_audio_file = fopen("output_audio.opus", "wb");
    g_video_file = fopen("output_video.h264", "wb");
    if (g_audio_file) {
        aosl_log(AOSL_LOG_INFO, "Saving received audio to output_audio.opus\n");
    }
    if (g_video_file) {
        aosl_log(AOSL_LOG_INFO, "Saving received video to output_video.h264\n");
    }

    /* Add media tracks we are willing to receive */
    tinyrtc_track_config_t video_config = {0};
    video_config.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_config.mid = "v0";
    video_config.codec_id = TINYRTC_CODEC_H264;
    tinyrtc_peer_connection_add_track(pc, &video_config);

    tinyrtc_track_config_t audio_config = {0};
    audio_config.kind = TINYRTC_TRACK_KIND_AUDIO;
    audio_config.mid = "a0";
    audio_config.codec_id = TINYRTC_CODEC_OPUS;
    tinyrtc_peer_connection_add_track(pc, &audio_config);

    if (auto_signaling) {
        /* Automatic signaling using public signaling server */
        aosl_log(AOSL_LOG_INFO, "Starting automatic signaling... room=%s server=%s\n",
                room_id, default_signaling_server);

        tinyrtc_signaling_config_t sig_config = {0};
        sig_config.url = (char *)default_signaling_server;
        sig_config.room_id = (char *)room_id;
        sig_config.client_id = NULL; /* auto-generate */
        sig_config.auto_connect = true;

        tinyrtc_signaling_t *sig = tinyrtc_signaling_create(
            ctx, &sig_config, signaling_callback, NULL);

        if (!sig) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create signaling client\n");
            goto cleanup;
        }

        tinyrtc_error_t err = tinyrtc_signaling_connect(sig);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to connect to signaling server: %s\n",
                    tinyrtc_get_error_string(err));
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Connected to signaling server, waiting for offer...\n");
        aosl_log(AOSL_LOG_INFO, "Open browser_test.html and enter room-id: %s\n", room_id);

        /* Poll until we get an offer */
        while (!g_got_offer && tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            tinyrtc_process_events(ctx, 100);
            aosl_msleep(100);
        }

        if (!g_got_offer || !g_pending_offer) {
            aosl_log(AOSL_LOG_ERROR, "Timed out waiting for offer\n");
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Got offer, setting remote description...\n");
        tinyrtc_error_t err2 = tinyrtc_peer_connection_set_remote_description(pc, g_pending_offer);
        aosl_free(g_pending_offer);
        g_pending_offer = NULL;

        if (err2 != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to set remote description: %s\n",
                    tinyrtc_get_error_string(err2));
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        /* Create answer */
        char *answer_sdp = NULL;
        tinyrtc_error_t err3 = tinyrtc_peer_connection_create_answer(pc, &answer_sdp);
        if (err3 != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create answer: %s\n",
                    tinyrtc_get_error_string(err3));
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Created answer, sending to signaling server...\n");
        tinyrtc_error_t err4 = tinyrtc_signaling_send_answer(sig, NULL, answer_sdp);
        if (err4 != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to send answer: %s\n",
                    tinyrtc_get_error_string(err4));
            tinyrtc_free(answer_sdp);
            tinyrtc_signaling_destroy(sig);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Answer sent successfully\n");
        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)\n");
        tinyrtc_free(answer_sdp);

        /* Main loop */
        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            tinyrtc_process_events(ctx, 10);
            aosl_msleep(10);
        }

        tinyrtc_signaling_destroy(sig);
    } else if (argc == 3 && strcmp(argv[1], "--offer") == 0) {
        /* Manual mode: Read offer from file */
        char *offer_sdp = demo_read_sdp(argv[2]);
        if (!offer_sdp) {
            aosl_log(AOSL_LOG_ERROR, "Failed to read offer from %s\n", argv[2]);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Setting remote description from offer...\n");
        tinyrtc_error_t err = tinyrtc_peer_connection_set_remote_description(pc, offer_sdp);
        tinyrtc_free(offer_sdp);

        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to set remote description: %s\n",
                    tinyrtc_get_error_string(err));
            goto cleanup;
        }

        /* Create answer */
        char *answer_sdp = NULL;
        err = tinyrtc_peer_connection_create_answer(pc, &answer_sdp);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create answer: %s\n",
                    tinyrtc_get_error_string(err));
            goto cleanup;
        }

        int wr = demo_write_sdp("answer.sdp", answer_sdp);
        if (wr != 0) {
            aosl_log(AOSL_LOG_ERROR, "Failed to write answer to answer.sdp\n");
            tinyrtc_free(answer_sdp);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Answer generated and saved to answer.sdp\n");
        aosl_log(AOSL_LOG_INFO, "Copy answer.sdp to browser test page to complete connection setup\n");
        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)\n");
        tinyrtc_free(answer_sdp);

        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            tinyrtc_process_events(ctx, 10);
            aosl_msleep(10);
        }
    } else {
        aosl_log(AOSL_LOG_INFO, "Usage:\n");
        aosl_log(AOSL_LOG_INFO, "  Automatic mode:  %s --room <room-id>\n", argv[0]);
        aosl_log(AOSL_LOG_INFO, "  Manual mode:     %s --offer <offer.sdp>\n", argv[0]);
        aosl_log(AOSL_LOG_INFO, "\n");
        aosl_log(AOSL_LOG_INFO, "In automatic mode, open browser_test.html with same room-id\n");
        aosl_log(AOSL_LOG_INFO, "and connection will be established automatically\n");
        goto cleanup;
    }

cleanup:
    if (g_pending_offer) {
        aosl_free(g_pending_offer);
    }
    /* Close output files */
    if (g_audio_file) {
        fclose(g_audio_file);
        aosl_log(AOSL_LOG_INFO, "Saved audio frames to output_audio.opus\n");
    }
    if (g_video_file) {
        fclose(g_video_file);
        aosl_log(AOSL_LOG_INFO, "Saved video frames to output_video.h264\n");
    }
    g_pc = NULL;
    tinyrtc_peer_connection_destroy(pc);
    tinyrtc_destroy(ctx);
    demo_exit_aosl();
    return 0;
}
