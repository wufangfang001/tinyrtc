/*
 * test_rtp.c
 *
 * Unit tests for RTP header parsing and building
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "media.h"

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
