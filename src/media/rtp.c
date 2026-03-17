/**
 * @file rtp.c
 * @brief RTP/RTCP protocol implementation according to RFC 3550
 *
 * This file contains RTP header parsing/building and RTCP processing.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "media.h"

/* =============================================================================
 * RTP header parsing and building
 * ========================================================================== */

/*
 * RTP header format (RFC 3550):
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |           timestamp          |           synchronization source (SSRC)
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |      contributing source (CSRC) identifiers, if any            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 header extension, if X=1                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

bool tinyrtc_rtp_parse_header(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtp_header_t *header)
{
    TINYRTC_CHECK_NULL(packet);
    TINYRTC_CHECK_NULL(header);

    /* Minimum length is 12 bytes for no CSRC */
    if (len < 12) {
        TINYRTC_LOG_DEBUG("rtp: packet too short: %zu < 12", len);
        return false;
    }

    /* First byte */
    uint8_t b0 = packet[0];
    header->version = (b0 >> 6) & 0x03;
    header->padding = ((b0 >> 5) & 0x01) != 0;
    header->extension = ((b0 >> 4) & 0x01) != 0;
    header->csrc_count = b0 & 0x0F;

    /* Check version - must be 2 */
    if (header->version != 2) {
        TINYRTC_LOG_DEBUG("rtp: invalid version: %d", header->version);
        return false;
    }

    /* Second byte */
    uint8_t b1 = packet[1];
    header->marker = ((b1 >> 7) & 0x01) != 0;
    header->payload_type = b1 & 0x7F;

    /* Sequence number */
    header->sequence = (packet[2] << 8) | packet[3];

    /* Timestamp */
    header->timestamp = (packet[4] << 24) | (packet[5] << 16) |
                        (packet[6] << 8) | packet[7];

    /* SSRC */
    header->ssrc = (packet[8] << 24) | (packet[9] << 16) |
                   (packet[10] << 8) | packet[11];

    /* CSRC list */
    header->num_csrc = header->csrc_count;
    for (int i = 0; i < header->num_csrc && i < 16; i++) {
        size_t off = 12 + i * 4;
        if (off + 4 > len) {
            TINYRTC_LOG_DEBUG("rtp: CSRC extends beyond packet");
            return false;
        }
        header->csrc[i] = (packet[off] << 24) | (packet[off+1] << 16) |
                          (packet[off+2] << 8) | packet[off+3];
    }

    /* Truncate if more than 16 (per struct limit) */
    if (header->num_csrc > 16) {
        header->num_csrc = 16;
    }

    /* Check total header length doesn't exceed packet */
    size_t header_len = 12 + 4 * header->num_csrc;
    if (header->extension) {
        /* Skip extension for now - we don't process it but need to account */
        if (header_len + 4 > len) {
            TINYRTC_LOG_DEBUG("rtp: extension header extends beyond packet");
            return false;
        }
        uint16_t ext_len = (packet[header_len + 2] << 8) | packet[header_len + 3];
        header_len += 4 + 4 * ext_len;
    }

    if (header_len > len) {
        TINYRTC_LOG_DEBUG("rtp: total header %zu exceeds packet %zu", header_len, len);
        return false;
    }

    return true;
}

size_t tinyrtc_rtp_build_header(
    const tinyrtc_rtp_header_t *header,
    uint8_t *buffer,
    size_t max_len)
{
    TINYRTC_CHECK(header != NULL, 0);
    TINYRTC_CHECK(buffer != NULL, 0);

    size_t header_len = 12 + 4 * header->num_csrc;
    /* We don't handle extensions for now */
    if (header->extension) {
        TINYRTC_LOG_DEBUG("rtp: extensions not supported in building");
        return 0;
    }
    if (header_len > max_len) {
        TINYRTC_LOG_DEBUG("rtp: header too big for buffer: %zu > %zu", header_len, max_len);
        return 0;
    }

    /* First byte */
    buffer[0] = ((header->version & 0x03) << 6) |
                ((header->padding ? 1 : 0) << 5) |
                ((header->extension ? 1 : 0) << 4) |
                (header->csrc_count & 0x0F);

    /* Second byte */
    buffer[1] = ((header->marker ? 1 : 0) << 7) |
                (header->payload_type & 0x7F);

    /* Sequence number */
    buffer[2] = (header->sequence >> 8) & 0xFF;
    buffer[3] = header->sequence & 0xFF;

    /* Timestamp */
    buffer[4] = (header->timestamp >> 24) & 0xFF;
    buffer[5] = (header->timestamp >> 16) & 0xFF;
    buffer[6] = (header->timestamp >> 8) & 0xFF;
    buffer[7] = header->timestamp & 0xFF;

    /* SSRC */
    buffer[8] = (header->ssrc >> 24) & 0xFF;
    buffer[9] = (header->ssrc >> 16) & 0xFF;
    buffer[10] = (header->ssrc >> 8) & 0xFF;
    buffer[11] = header->ssrc & 0xFF;

    /* CSRC */
    for (int i = 0; i < header->num_csrc; i++) {
        size_t off = 12 + i * 4;
        buffer[off] = (header->csrc[i] >> 24) & 0xFF;
        buffer[off+1] = (header->csrc[i] >> 16) & 0xFF;
        buffer[off+2] = (header->csrc[i] >> 8) & 0xFF;
        buffer[off+3] = header->csrc[i] & 0xFF;
    }

    return header_len;
}

const uint8_t *tinyrtc_rtp_get_payload(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtp_header_t *header)
{
    TINYRTC_CHECK_NULL(packet);
    TINYRTC_CHECK_NULL(header);

    size_t header_len = 12 + 4 * header->num_csrc;

    /* Skip extension if present */
    if (header->extension && header_len + 4 <= len) {
        uint16_t ext_len = (packet[header_len + 2] << 8) | packet[header_len + 3];
        header_len += 4 + 4 * ext_len;
    }

    if (header_len > len) {
        return NULL;
    }

    return packet + header_len;
}

/* =============================================================================
 * RTCP handling
 * ========================================================================== */

/*
 * RTCP common header format:
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |V=2|P|    RC   |   PT          |             length            |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 */

#define RTCP_VERSION 2

uint8_t tinyrtc_rtcp_get_type(const uint8_t *packet, size_t len)
{
    if (len < 4) {
        return 0xFF;
    }
    return packet[1] & 0xFF;
}

int tinyrtc_rtcp_get_report_count(const uint8_t *packet, size_t len)
{
    if (len < 4) {
        return 0;
    }
    return packet[0] & 0x1F; /* RC = reception report count */
}

bool tinyrtc_rtcp_parse_sender_report(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtcp_sender_report_t *sr)
{
    TINYRTC_CHECK_NULL(packet);
    TINYRTC_CHECK_NULL(sr);

    /* Sender report (PT 200) minimum size:
     * header (4) + ssrc (4) + 5 words = 4 + 4 + 5*4 = 28 bytes
     */
    if (len < 28) {
        return false;
    }

    /* Check version */
    if (((packet[0] >> 6) & 0x03) != RTCP_VERSION) {
        TINYRTC_LOG_DEBUG("rtcp: invalid version");
        return false;
    }

    /* Check packet type is sender report */
    if (tinyrtc_rtcp_get_type(packet, len) != 200) {
        TINYRTC_LOG_DEBUG("rtcp: not a sender report");
        return false;
    }

    /* Get length in 32-bit words */
    size_t length_words = (packet[2] << 8) | packet[3];
    size_t length_bytes = (length_words + 1) * 4;
    if (length_bytes > len) {
        TINYRTC_LOG_DEBUG("rtcp: length exceeds packet");
        return false;
    }

    /* Sender SSRC */
    sr->ssrc = (packet[4] << 24) | (packet[5] << 16) |
               (packet[6] << 8) | packet[7];

    /* NTP timestamp (64-bit) */
    uint32_t ntp_hi = (packet[8] << 24) | (packet[9] << 16) |
                       (packet[10] << 8) | packet[11];
    uint32_t ntp_lo = (packet[12] << 24) | (packet[13] << 16) |
                       (packet[14] << 8) | packet[15];
    sr->ntp_timestamp = ((uint64_t)ntp_hi << 32) | (uint64_t)ntp_lo;

    /* RTP timestamp */
    sr->rtp_timestamp = (packet[16] << 24) | (packet[17] << 16) |
                        (packet[18] << 8) | packet[19];

    /* Packet count */
    sr->packet_count = (packet[20] << 24) | (packet[21] << 16) |
                        (packet[22] << 8) | packet[23];

    /* octet count */
    sr->octet_count = (packet[24] << 24) | (packet[25] << 16) |
                       (packet[26] << 8) | packet[27];

    return true;
}

bool tinyrtc_rtcp_parse_report_block(
    int report_index,
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtcp_report_block_t *rb)
{
    TINYRTC_CHECK_NULL(packet);
    TINYRTC_CHECK_NULL(rb);

    /* Receiver report is after sender report header:
     * 28 bytes (SR header) + 24 bytes (each report block)
     */
    size_t offset = 28 + report_index * 24;
    if (offset + 24 > len) {
        return false;
    }

    /* SSRC of source being reported */
    rb->ssrc = (packet[offset] << 24) | (packet[offset+1] << 16) |
               (packet[offset+2] << 8) | packet[offset+3];

    /* fraction lost - 8 bits */
    rb->fraction_lost = packet[offset+4];

    /* cumulative lost - 24 bits */
    rb->cumulative_lost = (packet[offset+5] << 16) |
                           (packet[offset+6] << 8) |
                            packet[offset+7];
    /* The sign bit is already handled since it's uint32 and 24 bits */

    /* highest sequence number received */
    rb->highest_seq = (packet[offset+8] << 24) | (packet[offset+9] << 16) |
                       (packet[offset+10] << 8) | packet[offset+11];

    /* jitter */
    rb->jitter = (packet[offset+12] << 24) | (packet[offset+13] << 16) |
                 (packet[offset+14] << 8) | packet[offset+15];

    /* last SR timestamp */
    rb->lsr = (packet[offset+16] << 24) | (packet[offset+17] << 16) |
              (packet[offset+18] << 8) | packet[offset+19];

    /* delay since last SR */
    rb->dlsr = (packet[offset+20] << 24) | (packet[offset+21] << 16) |
               (packet[offset+22] << 8) | packet[offset+23];

    return true;
}

size_t tinyrtc_rtcp_build_sender_report(
    const tinyrtc_rtcp_sender_report_t *sr,
    uint8_t *buffer,
    size_t max_len)
{
    TINYRTC_CHECK(sr != NULL, 0);
    TINYRTC_CHECK(buffer != NULL, 0);

    /* Sender report with no receiver reports: 28 bytes */
    if (max_len < 28) {
        return 0;
    }

    /* Common header: V=2, P=0, RC=0, PT=200 */
    buffer[0] = (RTCP_VERSION << 6);
    buffer[1] = 200; /* PT = SR */

    /* Length: 28 bytes = 7 words (length = words - 1 = 6) */
    buffer[2] = 0;
    buffer[3] = 6;

    /* SSRC */
    buffer[4] = (sr->ssrc >> 24) & 0xFF;
    buffer[5] = (sr->ssrc >> 16) & 0xFF;
    buffer[6] = (sr->ssrc >> 8) & 0xFF;
    buffer[7] = sr->ssrc & 0xFF;

    /* NTP timestamp */
    buffer[8] = (sr->ntp_timestamp >> 56) & 0xFF;
    buffer[9] = (sr->ntp_timestamp >> 48) & 0xFF;
    buffer[10] = (sr->ntp_timestamp >> 40) & 0xFF;
    buffer[11] = (sr->ntp_timestamp >> 32) & 0xFF;
    buffer[12] = (sr->ntp_timestamp >> 24) & 0xFF;
    buffer[13] = (sr->ntp_timestamp >> 16) & 0xFF;
    buffer[14] = (sr->ntp_timestamp >> 8) & 0xFF;
    buffer[15] = sr->ntp_timestamp & 0xFF;

    /* RTP timestamp */
    buffer[16] = (sr->rtp_timestamp >> 24) & 0xFF;
    buffer[17] = (sr->rtp_timestamp >> 16) & 0xFF;
    buffer[18] = (sr->rtp_timestamp >> 8) & 0xFF;
    buffer[19] = sr->rtp_timestamp & 0xFF;

    /* packet count */
    buffer[20] = (sr->packet_count >> 24) & 0xFF;
    buffer[21] = (sr->packet_count >> 16) & 0xFF;
    buffer[22] = (sr->packet_count >> 8) & 0xFF;
    buffer[23] = sr->packet_count & 0xFF;

    /* octet count */
    buffer[24] = (sr->octet_count >> 24) & 0xFF;
    buffer[25] = (sr->octet_count >> 16) & 0xFF;
    buffer[26] = (sr->octet_count >> 8) & 0xFF;
    buffer[27] = sr->octet_count & 0xFF;

    return 28;
}

size_t tinyrtc_rtcp_build_receiver_report(
    const tinyrtc_rtcp_report_block_t *blocks,
    int num_blocks,
    uint8_t *buffer,
    size_t max_len)
{
    TINYRTC_CHECK(blocks != NULL, 0);
    TINYRTC_CHECK(buffer != NULL, 0);

    if (num_blocks < 0 || num_blocks > 31) {
        TINYRTC_LOG_DEBUG("rtcp: too many receiver reports: %d", num_blocks);
        return 0;
    }

    /* Receiver report (PT 201) - header 4 bytes + 24 bytes per block */
    size_t total_len = 4 + 24 * num_blocks;
    if (total_len > max_len) {
        TINYRTC_LOG_DEBUG("rtcp: receiver report too big: %zu > %zu", total_len, max_len);
        return 0;
    }

    /* Common header: V=2, P=0, RC=num_blocks, PT=201 */
    buffer[0] = (RTCP_VERSION << 6) | (num_blocks & 0x1F);
    buffer[1] = 201; /* PT = RR */

    /* length in words - (total_len / 4) - 1 */
    size_t len_words = (total_len / 4) - 1;
    buffer[2] = (len_words >> 8) & 0xFF;
    buffer[3] = len_words & 0xFF;

    /* Fill each report block */
    for (int i = 0; i < num_blocks; i++) {
        size_t offset = 4 + i * 24;
        const tinyrtc_rtcp_report_block_t *rb = &blocks[i];

        /* SSRC */
        buffer[offset] = (rb->ssrc >> 24) & 0xFF;
        buffer[offset+1] = (rb->ssrc >> 16) & 0xFF;
        buffer[offset+2] = (rb->ssrc >> 8) & 0xFF;
        buffer[offset+3] = rb->ssrc & 0xFF;

        /* fraction lost + cumulative lost */
        buffer[offset+4] = rb->fraction_lost;
        buffer[offset+5] = (rb->cumulative_lost >> 16) & 0xFF;
        buffer[offset+6] = (rb->cumulative_lost >> 8) & 0xFF;
        buffer[offset+7] = rb->cumulative_lost & 0xFF;

        /* highest sequence */
        buffer[offset+8] = (rb->highest_seq >> 24) & 0xFF;
        buffer[offset+9] = (rb->highest_seq >> 16) & 0xFF;
        buffer[offset+10] = (rb->highest_seq >> 8) & 0xFF;
        buffer[offset+11] = rb->highest_seq & 0xFF;

        /* jitter */
        buffer[offset+12] = (rb->jitter >> 24) & 0xFF;
        buffer[offset+13] = (rb->jitter >> 16) & 0xFF;
        buffer[offset+14] = (rb->jitter >> 8) & 0xFF;
        buffer[offset+15] = rb->jitter & 0xFF;

        /* LSR */
        buffer[offset+16] = (rb->lsr >> 24) & 0xFF;
        buffer[offset+17] = (rb->lsr >> 16) & 0xFF;
        buffer[offset+18] = (rb->lsr >> 8) & 0xFF;
        buffer[offset+19] = rb->lsr & 0xFF;

        /* DLSR */
        buffer[offset+20] = (rb->dlsr >> 24) & 0xFF;
        buffer[offset+21] = (rb->dlsr >> 16) & 0xFF;
        buffer[offset+22] = (rb->dlsr >> 8) & 0xFF;
        buffer[offset+23] = rb->dlsr & 0xFF;
    }

    return total_len;
}

/* =============================================================================
 * Jitter buffer implementation
 * ========================================================================== */

/* Internal jitter buffer structure */
struct tinyrtc_jitter_buffer {
    tinyrtc_jitter_config_t config;
    /* We use a simple sorted array based approach for now.
     * For embedded systems, this is acceptable with small buffer sizes.
     * Could be optimized to a circular buffer or skip list later.
     */
    uint8_t **packets;
    size_t *packet_len;
    uint16_t *sequence;
    uint64_t *arrival_time;
    size_t count;
    uint64_t next_playout_time;
};

void tinyrtc_jitter_get_default_config(tinyrtc_jitter_config_t *config)
{
    if (config != NULL) {
        config->min_delay_ms = 30;
        config->max_delay_ms = 200;
        config->buffer_size = 64;
    }
}

tinyrtc_jitter_buffer_t *tinyrtc_jitter_buffer_create(
    const tinyrtc_jitter_config_t *config)
{
    TINYRTC_CHECK_NULL(config);

    tinyrtc_jitter_buffer_t *jb = (tinyrtc_jitter_buffer_t *)tinyrtc_calloc(1, sizeof(*jb));
    if (jb == NULL) {
        return NULL;
    }

    jb->config = *config;

    /* Allocate packet array */
    jb->packets = (uint8_t **)tinyrtc_calloc(config->buffer_size, sizeof(uint8_t *));
    jb->packet_len = (size_t *)tinyrtc_calloc(config->buffer_size, sizeof(size_t));
    jb->sequence = (uint16_t *)tinyrtc_calloc(config->buffer_size, sizeof(uint16_t));
    jb->arrival_time = (uint64_t *)tinyrtc_calloc(config->buffer_size, sizeof(uint64_t));

    if (jb->packets == NULL || jb->packet_len == NULL ||
        jb->sequence == NULL || jb->arrival_time == NULL) {
        tinyrtc_jitter_buffer_destroy(jb);
        return NULL;
    }

    jb->count = 0;
    jb->next_playout_time = 0;

    return jb;
}

void tinyrtc_jitter_buffer_destroy(tinyrtc_jitter_buffer_t *jb)
{
    if (jb == NULL) {
        return;
    }

    /* Free individual packet buffers */
    if (jb->packets != NULL) {
        for (size_t i = 0; i < jb->count; i++) {
            if (jb->packets[i] != NULL) {
                tinyrtc_internal_free(jb->packets[i]);
            }
        }
        tinyrtc_internal_free(jb->packets);
    }
    tinyrtc_internal_free(jb->packet_len);
    tinyrtc_internal_free(jb->sequence);
    tinyrtc_internal_free(jb->arrival_time);
    tinyrtc_internal_free(jb);
}

tinyrtc_error_t tinyrtc_jitter_buffer_add_packet(
    tinyrtc_jitter_buffer_t *jb,
    const uint8_t *packet,
    size_t len,
    uint64_t timestamp)
{
    TINYRTC_CHECK(jb != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(packet != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (jb->count >= jb->config.buffer_size) {
        TINYRTC_LOG_WARN("jitter buffer full, dropping oldest packet");
        /* Drop the first (oldest) packet */
        if (jb->packets[0] != NULL) {
            tinyrtc_internal_free(jb->packets[0]);
        }
        /* Shift everything left */
        for (size_t i = 1; i < jb->count; i++) {
            jb->packets[i-1] = jb->packets[i];
            jb->packet_len[i-1] = jb->packet_len[i];
            jb->sequence[i-1] = jb->sequence[i];
            jb->arrival_time[i-1] = jb->arrival_time[i];
        }
        jb->count--;
    }

    /* Allocate buffer for packet copy */
    uint8_t *copy = (uint8_t *)tinyrtc_malloc(len);
    if (copy == NULL) {
        TINYRTC_LOG_ERROR("jitter buffer: out of memory");
        return TINYRTC_ERROR_MEMORY;
    }
    memcpy(copy, packet, len);

    /* Parse RTP header to get sequence number */
    tinyrtc_rtp_header_t header;
    if (!tinyrtc_rtp_parse_header(packet, len, &header)) {
        tinyrtc_internal_free(copy);
        return TINYRTC_ERROR_INVALID_ARG;
    }

    /* Insert in sorted order by sequence number */
    size_t insert_pos = jb->count;
    for (size_t i = 0; i < jb->count; i++) {
        /* Check sequence wrapping - we use 16-bit sequence numbers */
        int16_t diff = (int16_t)(header.sequence - jb->sequence[i]);
        if (diff < 0) {
            insert_pos = i;
            break;
        }
    }

    /* Shift to make room */
    for (size_t i = jb->count; i > insert_pos; i--) {
        jb->packets[i] = jb->packets[i-1];
        jb->packet_len[i] = jb->packet_len[i-1];
        jb->sequence[i] = jb->sequence[i-1];
        jb->arrival_time[i] = jb->arrival_time[i-1];
    }

    /* Insert new packet */
    jb->packets[insert_pos] = copy;
    jb->packet_len[insert_pos] = len;
    jb->sequence[insert_pos] = header.sequence;
    jb->arrival_time[insert_pos] = timestamp;
    jb->count++;

    /* Initialize next playout time on first packet */
    if (jb->count == 1) {
        jb->next_playout_time = timestamp + jb->config.min_delay_ms;
    }

    TINYRTC_LOG_DEBUG("jitter buffer: added seq %u at pos %zu", header.sequence, insert_pos);
    return TINYRTC_OK;
}

tinyrtc_error_t tinyrtc_jitter_buffer_get_packet(
    tinyrtc_jitter_buffer_t *jb,
    uint8_t *out_buffer,
    size_t max_len,
    uint64_t current_time_ms,
    size_t *out_len)
{
    TINYRTC_CHECK(jb != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(out_buffer != NULL, TINYRTC_ERROR_INVALID_ARG);
    TINYRTC_CHECK(out_len != NULL, TINYRTC_ERROR_INVALID_ARG);

    if (jb->count == 0) {
        return TINYRTC_ERROR_NOT_FOUND;
    }

    /* Check if the first packet is ready for playout */
    if (current_time_ms < jb->next_playout_time) {
        /* Not ready yet */
        return TINYRTC_ERROR_NOT_FOUND;
    }

    /* Get the first packet (earliest sequence) */
    uint8_t *packet = jb->packets[0];
    size_t len = jb->packet_len[0];

    if (len > max_len) {
        TINYRTC_LOG_ERROR("jitter buffer: output buffer too small");
        return TINYRTC_ERROR;
    }

    /* Copy to output */
    memcpy(out_buffer, packet, len);
    *out_len = len;

    /* Free and remove from buffer */
    tinyrtc_internal_free(packet);

    /* Shift everything left */
    for (size_t i = 1; i < jb->count; i++) {
        jb->packets[i-1] = jb->packets[i];
        jb->packet_len[i-1] = jb->packet_len[i];
        jb->sequence[i-1] = jb->sequence[i];
        jb->arrival_time[i-1] = jb->arrival_time[i];
    }
    jb->count--;

    /* Update next playout time based on jitter delay */
    jb->next_playout_time = current_time_ms + jb->config.min_delay_ms;

    /* Clamp to max delay */
    if (jb->next_playout_time - current_time_ms > jb->config.max_delay_ms) {
        jb->next_playout_time = current_time_ms + jb->config.max_delay_ms;
    }

    return TINYRTC_OK;
}

uint32_t tinyrtc_jitter_buffer_get_delay(tinyrtc_jitter_buffer_t *jb)
{
    if (jb == NULL || jb->count == 0) {
        return 0;
    }
    /* Simple approximation: count * average packet time
     * Assume 30 fps video -> ~33 ms per packet
     */
    uint32_t delay = (uint32_t)(jb->count * 33);
    if (delay > jb->config.max_delay_ms) {
        delay = (uint32_t)jb->config.max_delay_ms;
    }
    return delay;
}

/* =============================================================================
 * Frame packetization for common codecs
 * ========================================================================== */

int tinyrtc_packetize_frame(
    const uint8_t *frame,
    size_t frame_len,
    const tinyrtc_codec_packetization_t *params,
    tinyrtc_packet_callback_t callback,
    void *user_data)
{
    TINYRTC_CHECK(frame != NULL, 0);
    TINYRTC_CHECK(params != NULL, 0);
    TINYRTC_CHECK(callback != NULL, 0);

    if (params->mtu < 20) {
        TINYRTC_LOG_ERROR("packetize: MTU too small");
        return 0;
    }

    /* Maximum payload per packet = MTU - minimum RTP header (12 bytes) */
    size_t max_payload = params->mtu - 12;
    size_t offset = 0;
    int packets = 0;

    /* For simple packetization, just chunk it into MTU-sized blocks */
    while (offset < frame_len) {
        size_t chunk_len = frame_len - offset;
        if (chunk_len > max_payload) {
            chunk_len = max_payload;
        }

        /* Callback with the chunk - caller adds RTP header */
        callback(frame + offset, chunk_len, user_data);
        offset += chunk_len;
        packets++;
    }

    return packets;
}

bool tinyrtc_depacketize_frame(
    const uint8_t *packet,
    size_t len,
    uint8_t *frame_buffer,
    size_t max_len,
    size_t *frame_len)
{
    /* For simple depacketization with single packet per frame (common for video)
     * just copy the payload directly. For interleaved packetization like H.264,
     * this needs to be more complex handling NAL units.
     */
    TINYRTC_CHECK_NULL(packet);
    TINYRTC_CHECK_NULL(frame_buffer);
    TINYRTC_CHECK_NULL(frame_len);

    /* Parse RTP header to get to payload */
    tinyrtc_rtp_header_t header;
    if (!tinyrtc_rtp_parse_header(packet, len, &header)) {
        return false;
    }

    const uint8_t *payload = tinyrtc_rtp_get_payload(packet, len, &header);
    if (payload == NULL) {
        return false;
    }

    size_t payload_len = len - (size_t)(payload - packet);

    if (payload_len > max_len) {
        TINYRTC_LOG_DEBUG("depacketize: payload too large for frame buffer");
        return false;
    }

    memcpy(frame_buffer, payload, payload_len);
    *frame_len = payload_len;

    /* For single packet per frame, we're done immediately */
    return header.marker; /* marker bit indicates end of frame */
}
