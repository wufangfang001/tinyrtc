/*
 * test_rtp.c
 *
 * Unit tests for RTP header parsing and building
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "media.h"
#include <string.h>

typedef struct {
    int count;
    size_t lengths[8];
    bool markers[8];
    uint8_t payloads[8][1600];
} packetize_capture_t;

static void capture_packet_callback(
    const uint8_t *payload,
    size_t payload_len,
    bool marker,
    void *user_data)
{
    packetize_capture_t *capture = (packetize_capture_t *)user_data;
    int index = capture->count++;

    capture->lengths[index] = payload_len;
    capture->markers[index] = marker;
    memcpy(capture->payloads[index], payload, payload_len);
}

/* Test RTP header parsing */
MINUNIT_TEST(test_rtp_header_parse)
{
    // A valid RTP header with:
    // version 2, no padding, no extension, 0 CSRC, marker 0, payload type 96
    // sequence number 1234, timestamp 56789, SSRC 0x12345678
    uint8_t rtp_data[] = {
        0x80, 0x60, 0x04, 0xD2,  // version=2, marker=0, PT=96, seq=1234
        0x00, 0x00, 0xDE, 0x15,  // timestamp 56789
        0x12, 0x34, 0x56, 0x78   // SSRC
    };

    tinyrtc_rtp_header_t header;
    bool ret = tinyrtc_rtp_parse_header(rtp_data, sizeof(rtp_data), &header);

    MINUNIT_ASSERT(ret == true, "Failed to parse valid RTP header");
    MINUNIT_ASSERT(header.version == 2, "Wrong version");
    MINUNIT_ASSERT(header.padding == 0, "Wrong padding flag");
    MINUNIT_ASSERT(header.extension == 0, "Wrong extension flag");
    MINUNIT_ASSERT(header.csrc_count == 0, "Wrong CSRC count");
    MINUNIT_ASSERT(header.marker == 0, "Wrong marker flag");
    MINUNIT_ASSERT(header.payload_type == 96, "Wrong payload type");
    MINUNIT_ASSERT(header.sequence == 1234, "Wrong sequence number");
    // Network byte order (big-endian): 0x0000DE15 = 56853
    MINUNIT_ASSERT(header.timestamp == 56853, "Wrong timestamp");
    MINUNIT_ASSERT(header.ssrc == 0x12345678, "Wrong SSRC");

    return 0;
}

/* Test RTP header building */
MINUNIT_TEST(test_rtp_header_build)
{
    tinyrtc_rtp_header_t header = {
        .version = 2,
        .padding = 0,
        .extension = 0,
        .csrc_count = 0,
        .marker = 1,
        .payload_type = 100,
        .sequence = 4321,
        .timestamp = 98765,
        .ssrc = 0x87654321
    };

    uint8_t buffer[16];
    size_t ret = tinyrtc_rtp_build_header(&header, buffer, sizeof(buffer));

    MINUNIT_ASSERT(ret == 12, "Wrong RTP header size");
    MINUNIT_ASSERT(buffer[0] == 0x80, "Wrong first byte");
    MINUNIT_ASSERT((buffer[1] & 0x7F) == 100, "Wrong payload type");
    MINUNIT_ASSERT((buffer[1] & 0x80) != 0, "Marker bit not set");

    // Parse it back to verify
    tinyrtc_rtp_header_t parsed;
    bool parse_ret = tinyrtc_rtp_parse_header(buffer, ret, &parsed);

    MINUNIT_ASSERT(parse_ret == true, "Failed to parse built RTP header");
    MINUNIT_ASSERT(parsed.version == header.version, "Version mismatch");
    MINUNIT_ASSERT(parsed.marker == header.marker, "Marker mismatch");
    MINUNIT_ASSERT(parsed.payload_type == header.payload_type, "Payload type mismatch");
    MINUNIT_ASSERT(parsed.sequence == header.sequence, "Sequence mismatch");
    MINUNIT_ASSERT(parsed.timestamp == header.timestamp, "Timestamp mismatch");
    MINUNIT_ASSERT(parsed.ssrc == header.ssrc, "SSRC mismatch");

    return 0;
}

MINUNIT_TEST(test_rtp_packetize_h264_single_nalu_sets_marker_on_last_packet_only)
{
    uint8_t frame[3000];
    tinyrtc_codec_packetization_t params;
    packetize_capture_t capture;
    int packets;

    memset(frame, 0xAB, sizeof(frame));
    frame[0] = 0x65; /* IDR NALU */

    memset(&params, 0, sizeof(params));
    params.codec_id = TINYRTC_CODEC_H264;
    params.payload_type = 100;
    params.mtu = 1200;

    memset(&capture, 0, sizeof(capture));

    packets = tinyrtc_packetize_frame(frame, sizeof(frame), &params,
                                      capture_packet_callback, &capture);

    MINUNIT_ASSERT(packets == 3, "Expected large H264 NALU to be fragmented into three RTP payloads");
    MINUNIT_ASSERT(capture.count == 3, "Expected capture callback for each packet");
    MINUNIT_ASSERT(!capture.markers[0], "Expected first fragmented packet to clear marker");
    MINUNIT_ASSERT(!capture.markers[1], "Expected middle fragmented packet to clear marker");
    MINUNIT_ASSERT(capture.markers[2], "Expected final fragmented packet to set marker");

    MINUNIT_ASSERT((capture.payloads[0][0] & 0x1F) == 28, "Expected FU-A indicator for first packet");
    MINUNIT_ASSERT((capture.payloads[0][1] & 0x80) != 0, "Expected FU-A start bit on first packet");
    MINUNIT_ASSERT((capture.payloads[1][1] & 0x80) == 0, "Expected FU-A start bit cleared on middle packet");
    MINUNIT_ASSERT((capture.payloads[1][1] & 0x40) == 0, "Expected FU-A end bit cleared on middle packet");
    MINUNIT_ASSERT((capture.payloads[2][1] & 0x40) != 0, "Expected FU-A end bit on final packet");

    return 0;
}

MINUNIT_TEST(test_rtp_packetize_h264_annexb_splits_nalus_and_strips_start_codes)
{
    uint8_t frame[] = {
        0x00, 0x00, 0x00, 0x01, 0x67, 0x64, 0x00, 0x1F,
        0x00, 0x00, 0x01, 0x68, 0xEB, 0x73, 0x52
    };
    tinyrtc_codec_packetization_t params;
    packetize_capture_t capture;
    int packets;

    memset(&params, 0, sizeof(params));
    params.codec_id = TINYRTC_CODEC_H264;
    params.payload_type = 100;
    params.mtu = 1200;

    memset(&capture, 0, sizeof(capture));

    packets = tinyrtc_packetize_frame(frame, sizeof(frame), &params,
                                      capture_packet_callback, &capture);

    MINUNIT_ASSERT(packets == 2, "Expected Annex-B input to split into two H264 NALUs");
    MINUNIT_ASSERT(capture.count == 2, "Expected one RTP payload per small NALU");
    MINUNIT_ASSERT(capture.lengths[0] == 4, "Expected first NALU payload length without start code");
    MINUNIT_ASSERT(capture.lengths[1] == 4, "Expected second NALU payload length without start code");
    MINUNIT_ASSERT(memcmp(capture.payloads[0], frame + 4, 4) == 0,
                   "Expected first RTP payload to omit 4-byte start code");
    MINUNIT_ASSERT(memcmp(capture.payloads[1], frame + 11, 4) == 0,
                   "Expected second RTP payload to omit 3-byte start code");
    MINUNIT_ASSERT(!capture.markers[0], "Expected non-final NALU marker to be cleared");
    MINUNIT_ASSERT(capture.markers[1], "Expected final NALU marker to be set");

    return 0;
}

MINUNIT_TEST(test_rtp_depacketize_h264_fua_rebuilds_annexb_frame)
{
    uint8_t frame_part1[] = {
        0x80, 0x64, 0x00, 0x01,
        0x00, 0x00, 0x03, 0xE8,
        0x12, 0x34, 0x56, 0x78,
        0x7C, 0x85,
        0x11, 0x22, 0x33
    };
    uint8_t frame_part2[] = {
        0x80, 0xE4, 0x00, 0x02,
        0x00, 0x00, 0x03, 0xE8,
        0x12, 0x34, 0x56, 0x78,
        0x7C, 0x45,
        0x44, 0x55, 0x66
    };
    uint8_t frame_buffer[64];
    uint8_t full_frame[64];
    size_t frame_len = 0;
    size_t full_len = 0;
    bool frame_complete;
    const uint8_t expected[] = {
        0x00, 0x00, 0x00, 0x01,
        0x65, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66
    };

    frame_complete = tinyrtc_depacketize_frame(
        TINYRTC_CODEC_H264, frame_part1, sizeof(frame_part1), frame_buffer, sizeof(frame_buffer), &frame_len);
    MINUNIT_ASSERT(!frame_complete, "Expected first FU-A packet to wait for frame completion");
    memcpy(full_frame + full_len, frame_buffer, frame_len);
    full_len += frame_len;

    frame_complete = tinyrtc_depacketize_frame(
        TINYRTC_CODEC_H264, frame_part2, sizeof(frame_part2), frame_buffer, sizeof(frame_buffer), &frame_len);
    MINUNIT_ASSERT(frame_complete, "Expected final FU-A packet to complete frame");
    memcpy(full_frame + full_len, frame_buffer, frame_len);
    full_len += frame_len;
    MINUNIT_ASSERT(full_len == sizeof(expected), "Expected rebuilt H264 frame length to match");
    MINUNIT_ASSERT(memcmp(full_frame, expected, sizeof(expected)) == 0,
                   "Expected FU-A depacketization to rebuild Annex-B NALU");

    return 0;
}
