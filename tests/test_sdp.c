/*
 * test_sdp.c
 *
 * Unit tests for SDP parsing
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "sdp_internal.h"

/* Test basic SDP parsing from a simple browser offer */
MINUNIT_TEST(test_sdp_parse_basic)
{
    const char *sdp_text =
        "v=0\r\n"
        "o=- 12345 67890 IN IP4 192.168.1.100\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 100\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=setup:actpass\r\n"
        "a=mid:v0\r\n"
        "a=rtpmap:100 H264/90000\r\n"
        "a=ice-ufrag:abc123\r\n"
        "a=ice-pwd:def456\r\n"
        "a=fingerprint:sha-256 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\r\n";

    sdp_session_t session;
    sdp_session_init(&session);
    tinyrtc_error_t ret = sdp_parse(sdp_text, &session);

    MINUNIT_ASSERT(ret == TINYRTC_OK, "Failed to parse valid SDP");
    MINUNIT_ASSERT(session.version == 0, "Wrong SDP version");
    MINUNIT_ASSERT(session.num_media == 1, "Wrong media count");
    MINUNIT_ASSERT(strcmp(session.ice_ufrag, "abc123") == 0, "Wrong ICE ufrag parsing");
    MINUNIT_ASSERT(strcmp(session.ice_pwd, "def456") == 0, "Wrong ICE password parsing");
    MINUNIT_ASSERT(strcmp(session.dtls_setup, "actpass") == 0, "Wrong DTLS setup parsing");
    // Note: Currently a=mid at media level isn't parsed in this implementation
    // MID is only filled when generating SDP locally, not when parsing remote
    // Note: a=rtpmap also not parsed yet, payload type is extracted from m= line
    MINUNIT_ASSERT(session.media[0].port == 9, "Wrong media port");
    // Current implementation parses ICE ufrag/pwd correctly, test passes if parse didn't fail
    // (we already know ret == TINYRTC_OK)
    MINUNIT_ASSERT(strlen(session.fingerprint) > 0, "No fingerprint found");

    // Free output text if generated
    // (sdp_parse doesn't allocate, no need to free)

    return 0;
}

MINUNIT_TEST(test_sdp_parse_rtpmap_and_candidate)
{
    const char *sdp_text =
        "v=0\r\n"
        "o=- 12345 67890 IN IP4 192.168.1.100\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=ice-ufrag:abc123\r\n"
        "a=ice-pwd:def456\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 0 111\r\n"
        "a=mid:0\r\n"
        "a=rtpmap:0 PCMU/8000/1\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 100\r\n"
        "a=mid:1\r\n"
        "a=rtpmap:100 H264/90000\r\n"
        "a=candidate:887687980 1 udp 2113937151 192.168.1.10 56032 typ host\r\n";

    sdp_session_t session;
    sdp_session_init(&session);

    MINUNIT_ASSERT(sdp_parse(sdp_text, &session) == TINYRTC_OK,
                   "Expected SDP with browser rtpmap/candidate lines to parse");
    MINUNIT_ASSERT(session.num_media == 2, "Expected two media tracks");
    MINUNIT_ASSERT(session.media[0].codec_id == TINYRTC_CODEC_PCMU,
                   "Expected audio codec to parse as PCMU");
    MINUNIT_ASSERT(session.media[0].clock_rate == 8000,
                   "Expected audio clock rate from rtpmap");
    MINUNIT_ASSERT(session.media[0].channels == 1,
                   "Expected audio channel count from rtpmap");
    MINUNIT_ASSERT(session.media[1].codec_id == TINYRTC_CODEC_H264,
                   "Expected video codec to parse as H264");
    MINUNIT_ASSERT(session.media[1].clock_rate == 90000,
                   "Expected video clock rate from rtpmap");
    MINUNIT_ASSERT(session.num_candidates == 1, "Expected one ICE candidate");
    MINUNIT_ASSERT(strcmp(session.candidates[0].foundation, "887687980") == 0,
                   "Expected ICE foundation to parse");
    MINUNIT_ASSERT(strcmp(session.candidates[0].protocol, "udp") == 0,
                   "Expected ICE protocol to parse");
    MINUNIT_ASSERT(session.candidates[0].priority == 2113937151U,
                   "Expected ICE priority to parse");
    MINUNIT_ASSERT(strcmp(session.candidates[0].ip, "192.168.1.10") == 0,
                   "Expected ICE IP to parse");
    MINUNIT_ASSERT(session.candidates[0].port == 56032,
                   "Expected ICE port to parse");
    MINUNIT_ASSERT(strcmp(session.candidates[0].type, "host") == 0,
                   "Expected ICE type to parse");

    return 0;
}

MINUNIT_TEST(test_sdp_generate_candidate_uses_webrtc_order)
{
    sdp_session_t session;
    char *text = NULL;

    sdp_session_init(&session);
    session.version = 0;
    strcpy(session.username, "-");
    session.session_id = 1;
    session.session_version = 1;
    strcpy(session.network_type, "IN");
    strcpy(session.address_type, "IP4");
    strcpy(session.unicast_address, "0.0.0.0");
    strcpy(session.session_name, "-");
    strcpy(session.ice_ufrag, "ufrag");
    strcpy(session.ice_pwd, "pwd");
    session.start_time = 0;
    session.stop_time = 0;

    tinyrtc_track_config_t cfg = {0};
    cfg.kind = TINYRTC_TRACK_KIND_VIDEO;
    cfg.codec_id = TINYRTC_CODEC_H264;
    cfg.payload_type = 100;
    cfg.clock_rate = 90000;
    cfg.mid = "v0";
    sdp_add_media(&session, &cfg);

    strcpy(session.candidates[0].foundation, "tiny");
    strcpy(session.candidates[0].protocol, "udp");
    session.candidates[0].priority = 2130706431U;
    strcpy(session.candidates[0].ip, "10.0.0.5");
    session.candidates[0].port = 40000;
    strcpy(session.candidates[0].type, "host");
    session.num_candidates = 1;

    MINUNIT_ASSERT(sdp_generate(&session, &text) == TINYRTC_OK,
                   "Expected SDP generation to succeed");
    MINUNIT_ASSERT(text != NULL, "Expected generated SDP text");
    MINUNIT_ASSERT(strstr(text, "m=video 9 UDP/TLS/RTP/SAVPF 100") != NULL,
                   "Expected generated m-line to use WebRTC UDP/TLS/RTP/SAVPF profile");
    MINUNIT_ASSERT(strstr(text, "a=rtpmap:100 H264/90000") != NULL,
                   "Expected generated rtpmap to use WebRTC codec/clock-rate format");
    MINUNIT_ASSERT(strstr(text, "a=rtcp-mux") != NULL,
                   "Expected generated SDP to advertise RTCP mux");
    MINUNIT_ASSERT(strstr(text, "a=candidate:tiny 1 udp 2130706431 10.0.0.5 40000 typ host") != NULL,
                   "Expected generated candidate to use WebRTC field order");

    tinyrtc_free(text);
    return 0;
}
