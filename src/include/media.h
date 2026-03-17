/**
 * @file media.h
 * @brief Media stream handling
 */

#ifndef TINYRTC_MEDIA_H
#define TINYRTC_MEDIA_H

#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/peer_connection.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief RTP header structure
 */
typedef struct {
    uint8_t version;         /**< Version (2) */
    bool padding;             /**< Padding present */
    bool extension;           /**< Extension present */
    uint8_t csrc_count;      /**< Number of CSRC */
    bool marker;              /**< Marker bit */
    uint8_t payload_type;    /**< Payload type */
    uint16_t sequence;        /**< Sequence number */
    uint32_t timestamp;       /**< Timestamp */
    uint32_t ssrc;           /**< Synchronization source */
    int num_csrc;             /**< Number of CSRC identifiers */
    uint32_t csrc[16];      /**< CSRC */
} tinyrtc_rtp_header_t;

/**
 * @brief RTCP report block
 */
typedef struct {
    uint32_t ssrc;           /**< Source being reported on */
    uint8_t fraction_lost;   /**< Fraction lost since last report */
    uint32_t cumulative_lost;/**< Total packets lost */
    uint32_t highest_seq;    /**< Highest sequence number received */
    uint32_t jitter;          /**< Interarrival jitter */
    uint32_t lsr;             /**< Last sender report timestamp */
    uint32_t dlsr;            /**< Delay since last sender report */
} tinyrtc_rtcp_report_block_t;

/**
 * @brief RTCP sender report
 */
typedef struct {
    uint32_t ssrc;           /**< Sender SSRC */
    uint64_t ntp_timestamp;    /**< NTP timestamp */
    uint32_t rtp_timestamp;    /**< RTP timestamp */
    uint32_t packet_count;     /**< RTP packets sent */
    uint32_t octet_count;       /**< RTP octets sent */
} tinyrtc_rtcp_sender_report_t;

/**
 * @brief Jitter buffer configuration
 */
typedef struct {
    uint32_t min_delay_ms;       /**< Minimum delay in milliseconds */
    uint32_t max_delay_ms;       /**< Maximum delay in milliseconds */
    uint32_t buffer_size;        /**< Maximum buffer size in packets */
} tinyrtc_jitter_config_t;

/**
 * @brief Get default jitter buffer configuration
 *
 * @param config Output config
 */
void tinyrtc_jitter_get_default_config(tinyrtc_jitter_config_t *config);

/**
 * @brief Jitter buffer handle
 */
typedef struct tinyrtc_jitter_buffer tinyrtc_jitter_buffer_t;

/**
 * @brief Create new jitter buffer
 *
 * @param config Configuration
 * @return New jitter buffer
 */
tinyrtc_jitter_buffer_t *tinyrtc_jitter_buffer_create(
    const tinyrtc_jitter_config_t *config);

/**
 * @brief Destroy jitter buffer
 *
 * @param jb Jitter buffer to destroy
 */
void tinyrtc_jitter_buffer_destroy(tinyrtc_jitter_buffer_t *jb);

/**
 * @brief Add RTP packet to jitter buffer
 *
 * @param jb Jitter buffer
 * @param packet RTP packet data
 * @param len Packet length
 * @param timestamp Arrival timestamp in ms
 * @return TINYRTC_OK on success
 */
tinyrtc_error_t tinyrtc_jitter_buffer_add_packet(
    tinyrtc_jitter_buffer_t *jb,
    const uint8_t *packet,
    size_t len,
    uint64_t timestamp);

/**
 * @brief Get next playable packet from jitter buffer
 *
 * @param jb Jitter buffer
 * @param out_buffer Output buffer for packet
 * @param max_len Maximum buffer length
 * @param[out] out_len Output packet length
 * @param current_time_ms Current playback time
 * @return TINYRTC_OK if packet available
 */
tinyrtc_error_t tinyrtc_jitter_buffer_get_packet(
    tinyrtc_jitter_buffer_t *jb,
    uint8_t *out_buffer,
    size_t max_len,
    uint64_t current_time_ms,
    size_t *out_len);

/**
 * @brief Get estimated jitter buffer depth in milliseconds
 *
 * @param jb Jitter buffer
 * @return Current delay in ms
 */
uint32_t tinyrtc_jitter_buffer_get_delay(tinyrtc_jitter_buffer_t *jb);

/**
 * @brief Parse RTP header from packet
 *
 * @param packet Input packet data
 * @param len Packet length
 * @param header Output header structure
 * @return true if parsing succeeded
 */
bool tinyrtc_rtp_parse_header(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtp_header_t *header);

/**
 * @brief Build RTP header into packet buffer
 *
 * @param header Header to serialize
 * @param buffer Output buffer
 * @param max_len Maximum buffer size
 * @return Number of bytes written, 0 on error
 */
size_t tinyrtc_rtp_build_header(
    const tinyrtc_rtp_header_t *header,
    uint8_t *buffer,
    size_t max_len);

/**
 * @brief Get payload from RTP packet after header
 *
 * @param packet Full packet
 * @param len Packet length
 * @param header Parsed header
 * @return Pointer to payload data
 */
const uint8_t *tinyrtc_rtp_get_payload(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtp_header_t *header);

/**
 * @brief Parse RTCP packet
 *
 * @param packet RTCP packet data
 * @param len Packet length
 * @return Packet type (200-204, etc.)
 */
uint8_t tinyrtc_rtcp_get_type(const uint8_t *packet, size_t len);

/**
 * @brief Get number of receiver reports in RTCP
 *
 * @param packet RTCP packet
 * @param len Packet length
 * @return Number of report blocks
 */
int tinyrtc_rtcp_get_report_count(const uint8_t *packet, size_t len);

/**
 * @brief Parse RTCP sender report
 *
 * @param packet RTCP packet
 * @param len Packet length
 * @param sr Output sender report structure
 * @return true if parsing succeeded
 */
bool tinyrtc_rtcp_parse_sender_report(
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtcp_sender_report_t *sr);

/**
 * @brief Parse RTCP receiver report block
 *
 * @param report_index Index of report (0-based)
 * @param packet RTCP packet
 * @param len Packet length
 * @param rb Output report block
 * @return true if parsing succeeded
 */
bool tinyrtc_rtcp_parse_report_block(
    int report_index,
    const uint8_t *packet,
    size_t len,
    tinyrtc_rtcp_report_block_t *rb);

/**
 * @brief Build RTCP sender report packet
 *
 * @param sr Sender report
 * @param buffer Output buffer
 * @param max_len Maximum buffer length
 * @return Packet length, 0 on error
 */
size_t tinyrtc_rtcp_build_sender_report(
    const tinyrtc_rtcp_sender_report_t *sr,
    uint8_t *buffer,
    size_t max_len);

/**
 * @brief Build RTCP receiver report packet
 *
 * @param blocks Array of report blocks
 * @param num_blocks Number of blocks
 * @param buffer Output buffer
 * @param max_len Maximum buffer length
 * @return Packet length, 0 on error
 */
size_t tinyrtc_rtcp_build_receiver_report(
    const tinyrtc_rtcp_report_block_t *blocks,
    int num_blocks,
    uint8_t *buffer,
    size_t max_len);

/**
 * @brief Codec parameters for packetization
 */
typedef struct {
    int payload_type;
    uint32_t mtu;             /**< Maximum transmission unit */
} tinyrtc_codec_packetization_t;

/**
 * @brief Packetize a large frame into multiple RTP packets
 *
 * @param frame Input frame data
 * @param frame_len Frame length
 * @param params Packetization parameters
 * @param callback Callback for each packet
 * @param user_data User data for callback
 * @return Number of packets created
 */
typedef void (*tinyrtc_packet_callback_t)(
    const uint8_t *packet,
    size_t packet_len,
    void *user_data);

int tinyrtc_packetize_frame(
    const uint8_t *frame,
    size_t frame_len,
    const tinyrtc_codec_packetization_t *params,
    tinyrtc_packet_callback_t callback,
    void *user_data);

/**
 * @brief Depacketize RTP packets into complete frame
 *
 * @param packet RTP packet
 * @param len Packet length
 * @param frame_buffer Output frame buffer
 * @param max_len Maximum frame buffer size
 * @param[out] frame_len Output frame length
 * @return true if frame is complete
 */
bool tinyrtc_depacketize_frame(
    const uint8_t *packet,
    size_t len,
    uint8_t *frame_buffer,
    size_t max_len,
    size_t *frame_len);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_MEDIA_H */
