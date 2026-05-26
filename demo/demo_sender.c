/**
 * @file demo_sender.c
 * @brief Demo: WebRTC sender that sends video to browser
 *
 * Usage with automatic signaling:
 *   ./tinyrtc_send --room <room-id>
 *   Then open the sdp-transfer browser demo with the same room-id and it will connect automatically
 *
 * Usage with manual signaling (original):
 * 1. Run ./tinyrtc_send to generate offer
 * 2. Copy generated offer.sdp to your browser-side WebRTC SDP tool
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
    aosl_log(AOSL_LOG_INFO, "Got local ICE candidate: %s:%d type=%s\n",
            candidate->ip, candidate->port, candidate->type);
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

static void on_audio_frame(void *user_data, tinyrtc_track_t *track,
                            const uint8_t *frame, size_t frame_len, uint32_t timestamp)
{
    /* In a real application, you would decode and play this audio frame here.
     * TinyRTC handles packetization/depacketization internally - you get complete
     * encoded frames ready for decoding. */
    (void)user_data;
    (void)track;
    (void)frame;
    g_audio_frame_count++;
    if (g_audio_frame_count % 100 == 1) {
        aosl_log(AOSL_LOG_INFO, "Received audio frame #%d, size=%zu bytes, ts=%u\n",
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
    (void)frame;
    g_video_frame_count++;
    if (g_video_frame_count % 30 == 1) {
        aosl_log(AOSL_LOG_INFO, "Received video frame #%d, size=%zu bytes, ts=%u\n",
                g_video_frame_count, frame_len, timestamp);
    }
}

/* Global for signaling callback */
static tinyrtc_peer_connection_t *g_pc = NULL;
static bool g_got_answer = false;
static bool g_peer_joined = false;
static bool g_offer_sent = false;
static tinyrtc_track_t *g_video_track = NULL;
static tinyrtc_track_t *g_audio_track = NULL;

static void signaling_callback(tinyrtc_signal_event_t *event, void *user_data)
{
    (void)user_data;

    switch (event->type) {
        case TINYRTC_SIGNAL_EVENT_ANSWER:
            aosl_log(AOSL_LOG_INFO, "Received answer from signaling server\n");
            if (g_pc && event->data.answer) {
                tinyrtc_error_t err = tinyrtc_peer_connection_set_remote_description(g_pc, event->data.answer);
                if (err != TINYRTC_OK) {
                    aosl_log(AOSL_LOG_ERROR, "Failed to set remote answer: %s\n",
                            tinyrtc_get_error_string(err));
                } else {
                    aosl_log(AOSL_LOG_INFO, "Remote answer set successfully\n");
                    g_got_answer = true;
                }
                // NOTE: sig_process_message already frees event->data.answer after callback, don't free again!
            }
            break;
        case TINYRTC_SIGNAL_EVENT_PEER_JOIN:
            aosl_log(AOSL_LOG_INFO, "Peer joined room, ready to send offer\n");
            g_peer_joined = true;
            break;
        case TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE:
            aosl_log(AOSL_LOG_DEBUG, "Received ICE candidate from signaling\n");
            if (g_pc && event->data.candidate) {
                tinyrtc_error_t err = tinyrtc_peer_connection_add_ice_candidate(g_pc, event->data.candidate);
                if (err != TINYRTC_OK) {
                    aosl_log(AOSL_LOG_ERROR, "Failed to add ICE candidate: %s\n",
                            tinyrtc_get_error_string(err));
                } else {
                    aosl_log(AOSL_LOG_DEBUG, "ICE candidate added successfully\n");
                }
            }
            break;
        default:
            break;
    }
}

static void print_usage(const char *prog_name)
{
    printf("Usage: %s [options]\n\n", prog_name);
    printf("Options:\n");
    printf("  -h, --help              Show this help message\n");
    printf("  --room <room-id>        Use automatic signaling with specified room ID\n");
    printf("  --server <url>          Signaling server URL (default: ws://localhost:8765)\n");
    printf("  --stun <url>            STUN server URL (e.g., stun:stun.l.google.com:19302)\n");
    printf("  --no-verify             Skip SSL certificate verification (for self-signed certs)\n");
    printf("  --with-answer <file>    Use manual mode with answer from file\n");
    printf("  --audio-codec <codec>   Audio codec: g722, pcma, pcmu (default: g722)\n");
    printf("  --video-file <path>     Path to H.264 video file (default: test.264)\n");
    printf("  --audio-file <path>     Path to audio file (default: auto-detect based on codec)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  Automatic mode: %s --room my-room --server wss://your-server-ip:8766 --no-verify\n", prog_name);
    printf("  Manual mode:    %s --with-answer answer.sdp\n", prog_name);
    printf("  With PCMA audio: %s --room my-room --audio-codec pcma\n", prog_name);
    printf("  Custom files:   %s --room my-room --video-file my_video.264 --audio-file my_audio.pcmu\n", prog_name);
}

int main(int argc, char **argv)
{
    const char *room_id = "tinyrtc-demo";
    bool auto_signaling = false;
    const char *default_signaling_server = "ws://localhost:8765";
    const char *stun_server = "stun:stun.l.google.com:19302";  /* Default STUN server */
    bool disable_cert_verify = false;
    tinyrtc_codec_id_t audio_codec = TINYRTC_CODEC_G722;
    const char *audio_codec_name = "g722";
    const char *video_file_path = NULL;  /* NULL = use default test.264 */
    const char *audio_file_path = NULL;  /* NULL = auto-detect based on codec */

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        if (strcmp(argv[i], "--no-verify") == 0 || strcmp(argv[i], "-k") == 0) {
            disable_cert_verify = true;
        }
        if (strcmp(argv[i], "--stun") == 0 && i + 1 < argc) {
            stun_server = argv[i+1];
            i++;
        }
        if (strcmp(argv[i], "--video-file") == 0 && i + 1 < argc) {
            video_file_path = argv[i+1];
            i++;
        }
        if (strcmp(argv[i], "--audio-file") == 0 && i + 1 < argc) {
            audio_file_path = argv[i+1];
            i++;
        }
        if (strcmp(argv[i], "--audio-codec") == 0 && i + 1 < argc) {
            audio_codec_name = argv[i+1];
            if (strcasecmp(audio_codec_name, "g722") == 0) {
                audio_codec = TINYRTC_CODEC_G722;
            } else if (strcasecmp(audio_codec_name, "pcma") == 0) {
                audio_codec = TINYRTC_CODEC_PCMA;
            } else if (strcasecmp(audio_codec_name, "pcmu") == 0) {
                audio_codec = TINYRTC_CODEC_PCMU;
            } else {
                fprintf(stderr, "Unsupported audio codec: %s. Use g722, pcma, or pcmu.\n", audio_codec_name);
                return 1;
            }
            i++;
        }
    }

    if (argc >= 3 && strcmp(argv[1], "--room") == 0) {
        room_id = argv[2];
        auto_signaling = true;
    }
    /* Usage: ./tinyrtc_send --room room-id --server ws://your-server-ip:8765 */
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
    pc_config.stun_server = (char *)stun_server;  /* STUN server for NAT traversal */
    pc_config.observer.on_ice_candidate = on_ice_candidate;
    pc_config.observer.on_connection_state_change = on_connection_state_change;
    pc_config.observer.on_track_added = on_track_added;
    pc_config.observer.on_audio_frame = on_audio_frame;
    pc_config.observer.on_video_frame = on_video_frame;
    pc_config.is_initiator = true;

    tinyrtc_peer_connection_t *pc = tinyrtc_peer_connection_create(ctx, &pc_config);
    if (!pc) {
        aosl_log(AOSL_LOG_ERROR, "Failed to create peer connection\n");
        tinyrtc_destroy(ctx);
        return 1;
    }

    g_pc = pc;
    g_peer_joined = false;
    g_offer_sent = false;

    /* Add video track (we send video) */
    tinyrtc_track_config_t video_config = {0};
    video_config.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_config.mid = "v0";
    video_config.codec_id = TINYRTC_CODEC_H264;
    /* payload_type and clock_rate auto-filled from codec defaults */
    g_video_track = tinyrtc_peer_connection_add_track(pc, &video_config);

    /* Add audio track (we send audio) */
    tinyrtc_track_config_t audio_config = {0};
    audio_config.kind = TINYRTC_TRACK_KIND_AUDIO;
    audio_config.mid = "a0";
    audio_config.codec_id = audio_codec;
    /* payload_type and clock_rate auto-filled from codec defaults */
    g_audio_track = tinyrtc_peer_connection_add_track(pc, &audio_config);
    aosl_log(AOSL_LOG_INFO, "Audio codec set to: %s\n", tinyrtc_codec_get_name(audio_codec));

    /* Check for manual mode with answer from previous step */
    if (!auto_signaling && argc == 3 && strcmp(argv[1], "--with-answer") == 0) {
        /* Read answer from file */
        char *answer_sdp = demo_read_sdp(argv[2]);
        if (!answer_sdp) {
            aosl_log(AOSL_LOG_ERROR, "Failed to read answer from %s\n", argv[2]);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Setting remote description from answer...\n");
        tinyrtc_error_t err = tinyrtc_peer_connection_set_remote_description(pc, answer_sdp);
        tinyrtc_free(answer_sdp);

        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to set remote description: %s\n", tinyrtc_get_error_string(err));
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)\n");
        aosl_log(AOSL_LOG_INFO, "In a real application, you would read encoded frames from camera,\n");
        aosl_log(AOSL_LOG_INFO, "and call tinyrtc_track_send_video_frame() / tinyrtc_track_send_audio_frame() to send.\n");

        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            tinyrtc_process_events(ctx, 100);
            aosl_msleep(10);
        }
    } else if (auto_signaling) {
        /* Automatic signaling using external sdp-transfer server */
        aosl_log(AOSL_LOG_INFO, "Starting automatic signaling... room=%s server=%s\n",
                room_id, default_signaling_server);

        /* Open video and audio files before signaling connect */
        /* Open video file */
        FILE *video_file = NULL;
        if (video_file_path) {
            video_file = fopen(video_file_path, "rb");
            if (video_file) {
                aosl_log(AOSL_LOG_INFO, "Using custom video file: %s\n", video_file_path);
            } else {
                aosl_log(AOSL_LOG_ERROR, "Failed to open video file: %s\n", video_file_path);
            }
        } else {
            /* Try default video file locations */
            video_file = fopen("test.264", "rb");
            if (!video_file) {
                video_file = fopen("test_data/test.264", "rb");
            }
            if (video_file) {
                aosl_log(AOSL_LOG_INFO, "Found test.264, will send H.264 video frames\n");
            }
        }

        /* Open audio file */
        char audio_path[512];
        FILE *audio_file = NULL;

        if (audio_file_path) {
            audio_file = fopen(audio_file_path, "rb");
            if (audio_file) {
                aosl_log(AOSL_LOG_INFO, "Using custom audio file: %s\n", audio_file_path);
            } else {
                aosl_log(AOSL_LOG_ERROR, "Failed to open audio file: %s\n", audio_file_path);
            }
        } else {
            /* Try to find audio file in standard locations */
            /* Try current directory first with codec-specific name */
            snprintf(audio_path, sizeof(audio_path), "send_audio.%s", audio_codec_name);
            audio_file = fopen(audio_path, "rb");

            /* Try test_data directory */
            if (!audio_file) {
                snprintf(audio_path, sizeof(audio_path), "test_data/send_audio.%s", audio_codec_name);
                audio_file = fopen(audio_path, "rb");
            }

            /* Try agora_rtsa_sdk directory */
            if (!audio_file) {
                snprintf(audio_path, sizeof(audio_path), "/home/ubuntu/agora_rtsa_sdk/example/out/x86_64/send_audio.%s", audio_codec_name);
                audio_file = fopen(audio_path, "rb");
            }

            if (audio_file) {
                aosl_log(AOSL_LOG_INFO, "Found audio file: %s, will send %s audio frames\n", audio_path, tinyrtc_codec_get_name(audio_codec));
            } else {
                aosl_log(AOSL_LOG_WARNING, "Audio file not found: send_audio.%s\n", audio_codec_name);
            }
        }

        aosl_log(AOSL_LOG_INFO, "Starting signaling connect to %s room=%s\n",
                default_signaling_server, room_id);

        tinyrtc_signaling_config_t sig_config = {0};
        sig_config.url = (char *)default_signaling_server;
        sig_config.room_id = (char *)room_id;
        sig_config.client_id = NULL; /* auto-generate */
        sig_config.auto_connect = true;
        sig_config.disable_cert_verify = disable_cert_verify;

        aosl_log(AOSL_LOG_INFO, "Calling tinyrtc_signaling_create...\n");
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

        aosl_log(AOSL_LOG_INFO, "Connected to signaling server successfully\n");
        aosl_log(AOSL_LOG_INFO, "Waiting for peer to join before creating offer...\n");
        char *offer_sdp = NULL;
        aosl_log(AOSL_LOG_INFO, "Starting main loop... (Ctrl+C to exit)\n");

        /* Calculate timestamp increment for 30fps video */
        uint32_t video_timestamp = 0;
        uint32_t audio_timestamp = 0;
        const uint32_t video_inc = 90000 / 30;  /* 90kHz clock, 30fps */
        /* G.711/G.722 send 20ms frames */
        /* Note: G722 actual sampling rate is 16kHz, RTP clock rate is 8kHz per RFC 3551 */
        const uint32_t audio_clock_rate = tinyrtc_codec_get_clock_rate(audio_codec);
        const uint32_t audio_inc = audio_clock_rate / 50;  /* 20ms = 50 packets/sec */
        const int audio_frame_size = 160;  /* 160 bytes per 20ms frame for G711/G722 (64kbps) */

        /* Main loop: poll signaling and process TinyRTC events */
        int frame_count = 0;
        while (tinyrtc_peer_connection_get_state(pc) != TINYRTC_PC_STATE_CLOSED) {
            /* Process signaling messages */
            tinyrtc_process_events(ctx, 100);
            tinyrtc_signaling_process(sig);

            if (!g_offer_sent && g_peer_joined) {
                err = tinyrtc_peer_connection_create_offer(pc, &offer_sdp);
                if (err != TINYRTC_OK) {
                    aosl_log(AOSL_LOG_ERROR, "Failed to create offer: %s\n", tinyrtc_get_error_string(err));
                    tinyrtc_signaling_destroy(sig);
                    goto cleanup;
                }

                aosl_log(AOSL_LOG_INFO, "\n--- Generated SDP Offer (%zu bytes) ---\n", strlen(offer_sdp));
                aosl_log(AOSL_LOG_INFO, "%s", offer_sdp);
                aosl_log(AOSL_LOG_INFO, "--- End of SDP Offer ---\n\n");

                aosl_log(AOSL_LOG_INFO, "Created offer, sending to signaling server...\n");
                err = tinyrtc_signaling_send_offer(sig, NULL, offer_sdp);
                if (err != TINYRTC_OK) {
                    aosl_log(AOSL_LOG_ERROR, "Failed to send offer: %s\n",
                            tinyrtc_get_error_string(err));
                    tinyrtc_free(offer_sdp);
                    tinyrtc_signaling_destroy(sig);
                    goto cleanup;
                }

                g_offer_sent = true;
                aosl_log(AOSL_LOG_INFO, "Offer sent. Waiting for answer...\n");
            }

            /* If connected and we have a video file, send next NAL unit (frame) */
            if (tinyrtc_peer_connection_get_state(pc) == TINYRTC_PC_STATE_CONNECTED &&
                video_file && g_video_track) {
                /* Read one NAL unit (H.264 frame) - simple byte-by-byte scan for start code */
                uint8_t buffer[1024 * 1024];  /* 1MB max frame size */
                int pos = 0;
                int c;
                /* Skip any leading 0x00 */
                while ((c = fgetc(video_file)) != EOF) {
                    if (c != 0) break;
                    if (pos < (int)sizeof(buffer)) {
                        buffer[pos++] = c;
                    }
                }
                if (c == EOF) {
                    /* End of file, rewind */
                    rewind(video_file);
                    continue;
                }
                buffer[pos++] = c;
                /* Read until next start code (0x00 00 00 01) or EOF */
                int zero_count = 0;
                while ((c = fgetc(video_file)) != EOF) {
                    if (pos < (int)sizeof(buffer) - 4) {
                        buffer[pos++] = c;
                    }
                    if (c == 0) {
                        zero_count++;
                    } else {
                        if (zero_count >= 3 && c == 1) {
                            /* Found next start code - ungetc the last 4 bytes */
                            for (int i = 0; i < 4 && pos >= 4; i++) {
                                ungetc(buffer[--pos], video_file);
                            }
                            break;
                        }
                        zero_count = 0;
                    }
                }
                if (pos > 0) {
                    tinyrtc_track_send_video_frame(g_video_track, buffer, pos, video_timestamp);
                    video_timestamp += video_inc;
                    frame_count++;
                    if (frame_count % 30 == 1) {
                        aosl_log(AOSL_LOG_INFO, "Sent video frame #%d, size=%d bytes\n", frame_count, pos);
                    }
                }
            }

            /* Send audio frames from file if available */
            if (tinyrtc_peer_connection_get_state(pc) == TINYRTC_PC_STATE_CONNECTED &&
                audio_file && g_audio_track) {
                uint8_t audio_buffer[512];
                size_t bytes_read = fread(audio_buffer, 1, audio_frame_size, audio_file);
                if (bytes_read > 0) {
                    tinyrtc_track_send_audio_frame(g_audio_track, audio_buffer, bytes_read, audio_timestamp);
                    audio_timestamp += audio_inc;
                } else {
                    /* End of file, rewind */
                    rewind(audio_file);
                }
            }

            tinyrtc_process_events(ctx, 100);
            aosl_msleep(20);  /* 20ms interval for audio (50 fps) */
        }

        if (video_file) fclose(video_file);
        if (audio_file) fclose(audio_file);

        if (offer_sdp) {
            tinyrtc_free(offer_sdp);
        }
        tinyrtc_signaling_destroy(sig);
    } else {
        /* Manual mode: Create offer and write to file */
        char *offer_sdp = NULL;
        tinyrtc_error_t err = tinyrtc_peer_connection_create_offer(pc, &offer_sdp);
        if (err != TINYRTC_OK) {
            aosl_log(AOSL_LOG_ERROR, "Failed to create offer: %s\n", tinyrtc_get_error_string(err));
            goto cleanup;
        }

        int wr = demo_write_sdp("offer.sdp", offer_sdp);
        if (wr != 0) {
            aosl_log(AOSL_LOG_ERROR, "Failed to write offer to offer.sdp\n");
            tinyrtc_free(offer_sdp);
            goto cleanup;
        }

        aosl_log(AOSL_LOG_INFO, "Offer generated and saved to offer.sdp\n");
        aosl_log(AOSL_LOG_INFO, "Next step (manual mode):\n");
        aosl_log(AOSL_LOG_INFO, "  1. Open your browser-side WebRTC SDP tool or the sdp-transfer demo\n");
        aosl_log(AOSL_LOG_INFO, "  2. Paste offer.sdp content into browser\n");
        aosl_log(AOSL_LOG_INFO, "  3. Copy the generated answer from browser to answer.sdp\n");
        aosl_log(AOSL_LOG_INFO, "  4. Run: %s --with-answer answer.sdp\n", argv[0]);
        aosl_log(AOSL_LOG_INFO, "\n");
        aosl_log(AOSL_LOG_INFO, "Or use automatic mode:\n");
        aosl_log(AOSL_LOG_INFO, "  %s --room %s  (connects via configured signaling server)\n", argv[0], room_id);
        tinyrtc_free(offer_sdp);
    }

cleanup:
    g_pc = NULL;
    tinyrtc_peer_connection_destroy(pc);
    tinyrtc_destroy(ctx);
    demo_exit_aosl();
    return 0;
}
