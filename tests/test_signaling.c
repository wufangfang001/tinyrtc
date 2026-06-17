/*
 * test_signaling.c
 *
 * Unit tests for signaling message parsing compatibility.
 */

#include "minunit.h"
#include "tinyrtc/signaling.h"
#include "tinyrtc/peer_connection.h"
#include "peer_connection_internal.h"
#include "api/aosl.h"

#define sig_process_message tinyrtc_test_sig_process_message
#include "../src/signaling/signaling.c"
#undef sig_process_message

typedef struct {
    int called;
    tinyrtc_signal_event_type_t type;
    char offer[2048];
    char answer[2048];
    char foundation[64];
    char ip[128];
    char protocol[32];
    char cand_type[32];
    uint32_t priority;
    uint16_t port;
    bool is_ipv6;
} signaling_capture_t;

static void test_signal_callback(tinyrtc_signal_event_t *event, void *user_data)
{
    signaling_capture_t *cap = (signaling_capture_t *)user_data;
    cap->called++;
    cap->type = event->type;

    if (event->type == TINYRTC_SIGNAL_EVENT_OFFER && event->data.offer) {
        strncpy(cap->offer, event->data.offer, sizeof(cap->offer) - 1);
    } else if (event->type == TINYRTC_SIGNAL_EVENT_ANSWER && event->data.answer) {
        strncpy(cap->answer, event->data.answer, sizeof(cap->answer) - 1);
    } else if (event->type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE && event->data.candidate) {
        tinyrtc_ice_candidate_t *cand = event->data.candidate;
        if (cand->foundation) {
            strncpy(cap->foundation, cand->foundation, sizeof(cap->foundation) - 1);
        }
        if (cand->ip) {
            strncpy(cap->ip, cand->ip, sizeof(cap->ip) - 1);
        }
        if (cand->protocol) {
            strncpy(cap->protocol, cand->protocol, sizeof(cap->protocol) - 1);
        }
        if (cand->type) {
            strncpy(cap->cand_type, cand->type, sizeof(cap->cand_type) - 1);
        }
        cap->priority = cand->priority;
        cap->port = cand->port;
        cap->is_ipv6 = cand->is_ipv6;
    }
}

MINUNIT_TEST(test_signaling_parse_object_sdp_offer)
{
    struct tinyrtc_signaling sig;
    signaling_capture_t cap;
    const char *json =
        "{\"type\":\"offer\",\"sdp\":{\"type\":\"offer\",\"sdp\":\"v=0\\r\\n"
        "o=- 123 456 IN IP4 127.0.0.1\\r\\n"
        "s=-\\r\\n"
        "t=0 0\\r\\n\"}}";

    memset(&sig, 0, sizeof(sig));
    memset(&cap, 0, sizeof(cap));
    sig.callback = test_signal_callback;
    sig.user_data = &cap;

    tinyrtc_test_sig_process_message(&sig, (const uint8_t *)json, strlen(json));

    MINUNIT_ASSERT(cap.called == 1, "Expected signaling callback to be invoked");
    MINUNIT_ASSERT(cap.type == TINYRTC_SIGNAL_EVENT_OFFER, "Expected offer event type");
    MINUNIT_ASSERT(strcmp(cap.offer, "v=0\r\no=- 123 456 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n") == 0,
                   "Expected nested offer SDP object to be extracted as plain SDP string");

    return 0;
}

MINUNIT_TEST(test_signaling_parse_object_ice_candidate)
{
    struct tinyrtc_signaling sig;
    signaling_capture_t cap;
    const char *json =
        "{\"type\":\"ice-candidate\",\"candidate\":{\"candidate\":\"candidate:887687980 1 udp 2113937151 "
        "192.168.1.10 56032 typ host\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}}";

    memset(&sig, 0, sizeof(sig));
    memset(&cap, 0, sizeof(cap));
    sig.callback = test_signal_callback;
    sig.user_data = &cap;

    tinyrtc_test_sig_process_message(&sig, (const uint8_t *)json, strlen(json));

    MINUNIT_ASSERT(cap.called == 1, "Expected signaling callback to be invoked");
    MINUNIT_ASSERT(cap.type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE, "Expected ICE candidate event type");
    MINUNIT_ASSERT(strcmp(cap.foundation, "887687980") == 0, "Expected ICE foundation to be parsed");
    MINUNIT_ASSERT(strcmp(cap.ip, "192.168.1.10") == 0, "Expected ICE IP to be parsed");
    MINUNIT_ASSERT(cap.port == 56032, "Expected ICE port to be parsed");
    MINUNIT_ASSERT(strcmp(cap.protocol, "udp") == 0, "Expected ICE protocol to be parsed");
    MINUNIT_ASSERT(strcmp(cap.cand_type, "host") == 0, "Expected ICE candidate type to be parsed");
    MINUNIT_ASSERT(cap.priority == 2113937151U, "Expected ICE priority to be parsed");

    return 0;
}

MINUNIT_TEST(test_signaling_parse_string_ice_candidate)
{
    struct tinyrtc_signaling sig;
    signaling_capture_t cap;
    const char *json =
        "{\"type\":\"ice-candidate\",\"candidate\":\"candidate:887687980 1 udp 2113937151 "
        "192.168.1.10 56032 typ host\"}";

    memset(&sig, 0, sizeof(sig));
    memset(&cap, 0, sizeof(cap));
    sig.callback = test_signal_callback;
    sig.user_data = &cap;

    tinyrtc_test_sig_process_message(&sig, (const uint8_t *)json, strlen(json));

    MINUNIT_ASSERT(cap.called == 1, "Expected signaling callback to be invoked");
    MINUNIT_ASSERT(cap.type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE, "Expected ICE candidate event type");
    MINUNIT_ASSERT(strcmp(cap.foundation, "887687980") == 0, "Expected ICE foundation to be parsed from string form");
    MINUNIT_ASSERT(strcmp(cap.ip, "192.168.1.10") == 0, "Expected ICE IP to be parsed from string form");
    MINUNIT_ASSERT(cap.port == 56032, "Expected ICE port to be parsed from string form");

    return 0;
}

MINUNIT_TEST(test_signaling_parse_browser_tcp_ice_candidate)
{
    struct tinyrtc_signaling sig;
    signaling_capture_t cap;
    const char *json =
        "{\"type\":\"ice-candidate\",\"candidate\":{\"candidate\":\"candidate:1234567890 1 tcp 1518280447 "
        "192.168.1.10 9 typ host tcptype active\",\"sdpMid\":\"0\",\"sdpMLineIndex\":0}}";

    memset(&sig, 0, sizeof(sig));
    memset(&cap, 0, sizeof(cap));
    sig.callback = test_signal_callback;
    sig.user_data = &cap;

    tinyrtc_test_sig_process_message(&sig, (const uint8_t *)json, strlen(json));

    MINUNIT_ASSERT(cap.called == 1, "Expected signaling callback to be invoked");
    MINUNIT_ASSERT(cap.type == TINYRTC_SIGNAL_EVENT_ICE_CANDIDATE, "Expected ICE candidate event type");
    MINUNIT_ASSERT(strcmp(cap.foundation, "1234567890") == 0, "Expected ICE foundation to be parsed");
    MINUNIT_ASSERT(strcmp(cap.ip, "192.168.1.10") == 0, "Expected ICE IP to be parsed");
    MINUNIT_ASSERT(cap.port == 9, "Expected ICE port to be parsed");
    MINUNIT_ASSERT(strcmp(cap.protocol, "tcp") == 0, "Expected ICE protocol to be parsed");
    MINUNIT_ASSERT(strcmp(cap.cand_type, "host") == 0, "Expected ICE candidate type to be parsed");
    MINUNIT_ASSERT(cap.priority == 1518280447U, "Expected ICE priority to be parsed");

    return 0;
}

MINUNIT_TEST(test_signaling_parse_peer_joined_event)
{
    struct tinyrtc_signaling sig;
    signaling_capture_t cap;
    const char *json = "{\"type\":\"peer-joined\"}";

    memset(&sig, 0, sizeof(sig));
    memset(&cap, 0, sizeof(cap));
    sig.callback = test_signal_callback;
    sig.user_data = &cap;

    tinyrtc_test_sig_process_message(&sig, (const uint8_t *)json, strlen(json));

    MINUNIT_ASSERT(cap.called == 1, "Expected signaling callback to be invoked for peer-joined");
    MINUNIT_ASSERT(cap.type == TINYRTC_SIGNAL_EVENT_PEER_JOIN, "Expected peer-joined event type");

    return 0;
}

MINUNIT_TEST(test_signaling_tls_client_config_defaults)
{
    struct tinyrtc_signaling sig;
    int ret;

    memset(&sig, 0, sizeof(sig));
    mbedtls_entropy_init(&sig.entropy);
    mbedtls_ctr_drbg_init(&sig.ctr_drbg);
    mbedtls_ssl_config_init(&sig.conf);

    ret = mbedtls_ctr_drbg_seed(&sig.ctr_drbg, mbedtls_entropy_func,
                                &sig.entropy,
                                (const unsigned char *)"tinyrtc-test",
                                strlen("tinyrtc-test"));
    MINUNIT_ASSERT(ret == 0, "Expected CTR-DRBG seed to succeed");

    ret = sig_configure_tls_client(&sig, false, false);
    MINUNIT_ASSERT(ret == 0, "Expected TLS client configuration helper to succeed");
    MINUNIT_ASSERT(sig.conf.endpoint == MBEDTLS_SSL_IS_CLIENT,
                   "Expected TLS config endpoint to be client");
    MINUNIT_ASSERT(sig.conf.transport == MBEDTLS_SSL_TRANSPORT_STREAM,
                   "Expected TLS config transport to be stream");

    mbedtls_ssl_config_free(&sig.conf);
    mbedtls_ctr_drbg_free(&sig.ctr_drbg);
    mbedtls_entropy_free(&sig.entropy);

    return 0;
}

MINUNIT_TEST(test_peer_connection_create_answer_adds_remote_candidates_to_ice)
{
    tinyrtc_context_t ctx;
    tinyrtc_peer_connection_t *pc;
    tinyrtc_pc_config_t config;
    tinyrtc_track_config_t video_cfg;
    int old_log_level;
    const char *offer_sdp =
        "v=0\r\n"
        "o=- 100 100 IN IP4 0.0.0.0\r\n"
        "s=TinyRTC\r\n"
        "t=0 0\r\n"
        "a=ice-ufrag:remoteufrag\r\n"
        "a=ice-pwd:remotepwd\r\n"
        "a=setup:actpass\r\n"
        "m=video 9 RTP/SAVPF 100\r\n"
        "a=rtpmap:100 H264/90000\r\n"
        "a=mid:v0\r\n"
        "a=sendrecv\r\n"
        "a=candidate:rem1 1 udp 2113937151 127.0.0.1 50000 typ host\r\n"
        "a=candidate:rem2 1 udp 2113937150 0.0.0.0 50000 typ host\r\n";
    char *answer_sdp = NULL;
    tinyrtc_error_t err;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mutex = aosl_lock_create();
    MINUNIT_ASSERT(ctx.mutex != NULL, "Expected TinyRTC context mutex to be created");

    memset(&config, 0, sizeof(config));
    config.stun_server = NULL;
    config.is_initiator = false;

    pc = tinyrtc_peer_connection_create(&ctx, &config);
    MINUNIT_ASSERT(pc != NULL, "Expected peer connection to be created");

    memset(&video_cfg, 0, sizeof(video_cfg));
    video_cfg.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_cfg.mid = "v0";
    video_cfg.codec_id = TINYRTC_CODEC_H264;
    MINUNIT_ASSERT(tinyrtc_peer_connection_add_track(pc, &video_cfg) != NULL,
                   "Expected local track to be added");

    err = tinyrtc_peer_connection_set_remote_description(pc, offer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected remote description to parse");
    MINUNIT_ASSERT(pc->remote_sdp.num_candidates == 2, "Expected two remote candidates in parsed SDP");
    MINUNIT_ASSERT(pc->ice == NULL, "Expected ICE session not to exist before create_answer");

    old_log_level = aosl_get_log_level();
    aosl_set_log_level(AOSL_LOG_CRIT);
    err = tinyrtc_peer_connection_create_answer(pc, &answer_sdp);
    aosl_set_log_level(old_log_level);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected answer creation to succeed");
    MINUNIT_ASSERT(answer_sdp != NULL, "Expected generated answer SDP");
    MINUNIT_ASSERT(pc->ice != NULL, "Expected ICE session to be created");
    MINUNIT_ASSERT(pc->local_sdp.ice_ufrag[0] != '\0',
                   "Expected answer path to populate local ICE ufrag");
    MINUNIT_ASSERT(pc->local_sdp.ice_pwd[0] != '\0',
                   "Expected answer path to populate local ICE password");
    MINUNIT_ASSERT(pc->local_sdp.num_media == 1, "Expected one media line in generated answer");
    MINUNIT_ASSERT(strcmp(pc->local_sdp.media[0].mid, "v0") == 0,
                   "Expected answer to preserve remote offer mid");
    MINUNIT_ASSERT(pc->local_sdp.media[0].payload_type == 100,
                   "Expected answer to preserve remote offer payload type");
    MINUNIT_ASSERT(pc->ice->num_remote_candidates == 2,
                   "Expected parsed remote candidates to be copied into ICE session");
    if (pc->ice->num_local_candidates > 0) {
        MINUNIT_ASSERT(pc->ice->num_check_pairs > 0,
                       "Expected ICE check pairs when local candidates exist");
    }

    tinyrtc_free(answer_sdp);
    tinyrtc_peer_connection_destroy(pc);
    aosl_lock_destroy(ctx.mutex);

    return 0;
}

MINUNIT_TEST(test_peer_connection_create_answer_preserves_offer_media_order_and_mid)
{
    tinyrtc_context_t ctx;
    tinyrtc_peer_connection_t *pc;
    tinyrtc_pc_config_t config;
    tinyrtc_track_config_t audio_cfg;
    tinyrtc_track_config_t video_cfg;
    char *answer_sdp = NULL;
    tinyrtc_error_t err;
    int old_log_level;
    const char *offer_sdp =
        "v=0\r\n"
        "o=- 100 100 IN IP4 0.0.0.0\r\n"
        "s=TinyRTC\r\n"
        "t=0 0\r\n"
        "a=ice-ufrag:remoteufrag\r\n"
        "a=ice-pwd:remotepwd\r\n"
        "a=setup:actpass\r\n"
        "m=audio 9 RTP/SAVPF 111\r\n"
        "a=rtpmap:111 OPUS/48000/2\r\n"
        "a=mid:0\r\n"
        "a=sendrecv\r\n"
        "m=video 9 RTP/SAVPF 96\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=mid:1\r\n"
        "a=sendrecv\r\n";

    memset(&ctx, 0, sizeof(ctx));
    ctx.mutex = aosl_lock_create();
    MINUNIT_ASSERT(ctx.mutex != NULL, "Expected TinyRTC context mutex to be created");

    memset(&config, 0, sizeof(config));
    config.stun_server = NULL;
    config.is_initiator = false;

    pc = tinyrtc_peer_connection_create(&ctx, &config);
    MINUNIT_ASSERT(pc != NULL, "Expected peer connection to be created");

    memset(&video_cfg, 0, sizeof(video_cfg));
    video_cfg.kind = TINYRTC_TRACK_KIND_VIDEO;
    video_cfg.mid = "v0";
    video_cfg.codec_id = TINYRTC_CODEC_H264;
    MINUNIT_ASSERT(tinyrtc_peer_connection_add_track(pc, &video_cfg) != NULL,
                   "Expected local video track to be added");

    memset(&audio_cfg, 0, sizeof(audio_cfg));
    audio_cfg.kind = TINYRTC_TRACK_KIND_AUDIO;
    audio_cfg.mid = "a0";
    audio_cfg.codec_id = TINYRTC_CODEC_G722;
    MINUNIT_ASSERT(tinyrtc_peer_connection_add_track(pc, &audio_cfg) != NULL,
                   "Expected local audio track to be added");

    err = tinyrtc_peer_connection_set_remote_description(pc, offer_sdp);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected remote description to parse");

    old_log_level = aosl_get_log_level();
    aosl_set_log_level(AOSL_LOG_CRIT);
    err = tinyrtc_peer_connection_create_answer(pc, &answer_sdp);
    aosl_set_log_level(old_log_level);

    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected answer creation to succeed");
    MINUNIT_ASSERT(pc->local_sdp.num_media == 2, "Expected two media lines in answer");
    MINUNIT_ASSERT(pc->local_sdp.media[0].kind == TINYRTC_TRACK_KIND_AUDIO,
                   "Expected answer m-line 0 to stay audio");
    MINUNIT_ASSERT(strcmp(pc->local_sdp.media[0].mid, "0") == 0,
                   "Expected answer audio mid to match offer");
    MINUNIT_ASSERT(pc->local_sdp.media[0].payload_type == 111,
                   "Expected answer audio payload type to match offer");
    MINUNIT_ASSERT(pc->local_sdp.media[1].kind == TINYRTC_TRACK_KIND_VIDEO,
                   "Expected answer m-line 1 to stay video");
    MINUNIT_ASSERT(strcmp(pc->local_sdp.media[1].mid, "1") == 0,
                   "Expected answer video mid to match offer");
    MINUNIT_ASSERT(pc->local_sdp.media[1].payload_type == 96,
                   "Expected answer video payload type to match offer");

    tinyrtc_free(answer_sdp);
    tinyrtc_peer_connection_destroy(pc);
    aosl_lock_destroy(ctx.mutex);

    return 0;
}

MINUNIT_TEST(test_peer_connection_ignores_remote_tcp_candidate_for_udp_ice)
{
    tinyrtc_context_t ctx;
    tinyrtc_peer_connection_t *pc;
    tinyrtc_pc_config_t config;
    tinyrtc_ice_candidate_t candidate = {0};
    tinyrtc_error_t err;

    memset(&ctx, 0, sizeof(ctx));
    ctx.mutex = aosl_lock_create();
    MINUNIT_ASSERT(ctx.mutex != NULL, "Expected TinyRTC context mutex to be created");

    memset(&config, 0, sizeof(config));
    config.is_initiator = false;

    pc = tinyrtc_peer_connection_create(&ctx, &config);
    MINUNIT_ASSERT(pc != NULL, "Expected peer connection to be created");

    candidate.foundation = "tcp1";
    candidate.protocol = "tcp";
    candidate.ip = "192.168.1.10";
    candidate.port = 9;
    candidate.type = "host";
    candidate.priority = 1518280447U;
    candidate.is_ipv6 = false;

    err = tinyrtc_peer_connection_add_ice_candidate(pc, &candidate);
    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected unsupported TCP candidate to be ignored cleanly");
    MINUNIT_ASSERT(pc->remote_sdp.num_candidates == 0,
                   "Expected unsupported TCP candidate not to be added to remote SDP");

    tinyrtc_peer_connection_destroy(pc);
    aosl_lock_destroy(ctx.mutex);

    return 0;
}
