/**
 * @file congestion_control.h
 * @brief Congestion control for WebRTC - bandwidth estimation and rate adaptation
 *
 * This implements a simplified congestion control based on the principles from
 * Google Congestion Control (GCC) used in WebRTC. It uses delay-based
 * bandwidth estimation and AIMD rate adjustment.
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#ifndef TINYRTC_CONGESTION_CONTROL_H
#define TINYRTC_CONGESTION_CONTROL_H

#include "common.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Congestion control configuration
 */
typedef struct {
    uint32_t min_bitrate_bps;       /**< Minimum bitrate */
    uint32_t max_bitrate_bps;       /**< Maximum bitrate */
    uint32_t initial_bitrate_bps;    /**< Initial bitrate */
    bool enable_pacing;              /**< Enable packet pacing */
} tinyrtc_cc_config_t;

/**
 * @brief Congestion control handle
 */
typedef struct tinyrtc_cc tinyrtc_cc_t;

/**
 * @brief Get default congestion control configuration
 *
 * @param config Output configuration
 */
void tinyrtc_cc_get_default_config(tinyrtc_cc_config_t *config);

/**
 * @brief Create a new congestion control instance
 *
 * @param config Configuration
 * @return New congestion control handle, NULL on error
 */
tinyrtc_cc_t *tinyrtc_cc_create(const tinyrtc_cc_config_t *config);

/**
 * @brief Destroy a congestion control instance
 *
 * @param cc Congestion control to destroy
 */
void tinyrtc_cc_destroy(tinyrtc_cc_t *cc);

/**
 * @brief Get current estimated bandwidth in bits per second
 *
 * @param cc Congestion control
 * @return Current estimated bandwidth (bps)
 */
uint32_t tinyrtc_cc_get_estimated_bandwidth(tinyrtc_cc_t *cc);

/**
 * @brief Get current target send bitrate in bits per second
 *
 * @param cc Congestion control
 * @return Target send bitrate (bps)
 */
uint32_t tinyrtc_cc_get_target_bitrate(tinyrtc_cc_t *cc);

/**
 * @brief Report an outgoing packet
 *
 * Called when a packet is sent to update congestion control state.
 *
 * @param cc Congestion control
 * @param sequence_number RTP sequence number
 * @param timestamp RTP timestamp
 * @param send_time_ms Send time in milliseconds
 * @param size_bytes Packet size in bytes
 */
void tinyrtc_cc_report_packet_sent(
    tinyrtc_cc_t *cc,
    uint16_t sequence_number,
    uint32_t timestamp,
    uint64_t send_time_ms,
    size_t size_bytes);

/**
 * @brief Report a received receiver feedback (RTCP receiver report)
 *
 * This contains the feedback information from the receiver that
 * we use to update our bandwidth estimate.
 *
 * @param cc Congestion control
 * @param sequence_number Last received sequence number
 * @param timestamp RTP timestamp
 * @param receive_time_ms Local receive time in milliseconds
 * @param rtt_ms Round trip time in milliseconds
 * @param fraction_lost Fraction packet loss (0-255)
 * @param total_bytes Total bytes received
 * @param total_packets Total packets received
 */
void tinyrtc_cc_report_feedback(
    tinyrtc_cc_t *cc,
    uint16_t sequence_number,
    uint32_t timestamp,
    uint64_t receive_time_ms,
    uint32_t rtt_ms,
    uint8_t fraction_lost,
    uint64_t total_bytes,
    uint32_t total_packets);

/**
 * @brief Check if we should send this packet now (pacing)
 *
 * When pacing is enabled, this checks if we can send this packet now
 * based on current estimated bandwidth.
 *
 * @param cc Congestion control
 * @param packet_size_bytes Size of packet about to be sent
 * @param now_ms Current time in milliseconds
 * @return true if can send now, false if should wait
 */
bool tinyrtc_cc_can_send(
    tinyrtc_cc_t *cc,
    size_t packet_size_bytes,
    uint64_t now_ms);

/**
 * @brief Calculate how long to wait before next packet (pacing)
 *
 * @param cc Congestion control
 * @return Wait time in milliseconds
 */
uint32_t tinyrtc_cc_get_wait_time_ms(tinyrtc_cc_t *cc);

/**
 * @brief Report congestion detected (e.g., from packet loss)
 *
 * Forces a reduction in sending rate.
 *
 * @param cc Congestion control
 */
void tinyrtc_cc_report_congestion(tinyrtc_cc_t *cc);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_CONGESTION_CONTROL_H */
