/**
 * @file congestion_control.c
 * @brief Congestion control implementation for TinyRTC
 *
 * This implements a simplified congestion control based on Google Congestion Control (GCC)
 * principles: delay-based bandwidth estimation + AIMD rate adjustment.
 *
 * Reference: "A Google Congestion Control Algorithm for Real-Time Communication"
 *
 * Copyright (c) 20026
 * Licensed under MIT License
 */

#include "common.h"
#include "congestion_control.h"

/* =============================================================================
 * Internal Constants
 * ========================================================================== */

/* Default configuration values */
#define TINYRTC_CC_DEFAULT_MIN_BITRATE    500000       /* 500 kbps */
#define TINYRTC_CC_DEFAULT_MAX_BITRATE    20000000     /* 20 Mbps */
#define TINYRTC_CC_DEFAULT_INIT_BITRATE   2000000      /* 2 Mbps */

/* AIMD rate adjustment parameters */
#define TINYRTC_CC_ADD_INCREASE       100000          /* Add 100 kbps per RTT when no loss */
#define TINYRTC_CC_MULTIPLY_DECREASE   0.5            /* Cut in half when congestion detected */

/* Bandwidth estimation smoothing factor */
#define TINYRTC_CC_ESTIMATE_ALPHA      0.85

/* =============================================================================
 * Internal structure
 * ========================================================================== */

struct tinyrtc_cc {
    tinyrtc_cc_config_t config;

    /* Current state */
    uint32_t estimated_bandwidth_bps;
    uint32_t target_bitrate_bps;

    /* Packet history for estimation */
    uint64_t total_bytes_sent;
    uint32_t total_packets_sent;
    uint64_t last_update_time_ms;

    /* Pacing state */
    uint32_t bytes_this_interval;
    uint64_t pacing_next_send_time_ms;

    /* Recent feedback */
    double latest_rtt_ms;
    double latest_fraction_lost;
};

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

void tinyrtc_cc_get_default_config(tinyrtc_cc_config_t *config)
{
    if (config == NULL) {
        return;
    }
    config->min_bitrate_bps = TINYRTC_CC_DEFAULT_MIN_BITRATE;
    config->max_bitrate_bps = TINYRTC_CC_DEFAULT_MAX_BITRATE;
    config->initial_bitrate_bps = TINYRTC_CC_DEFAULT_INIT_BITRATE;
    config->enable_pacing = true;
}

tinyrtc_cc_t *tinyrtc_cc_create(const tinyrtc_cc_config_t *config)
{
    TINYRTC_CHECK_NULL(config);

    tinyrtc_cc_t *cc = (tinyrtc_cc_t *)tinyrtc_calloc(1, sizeof(*cc));
    if (cc == NULL) {
        TINYRTC_LOG_ERROR("cc_create: memory allocation failed");
        return NULL;
    }

    cc->config = *config;

    /* Initialize with starting bitrate */
    cc->estimated_bandwidth_bps = config->initial_bitrate_bps;
    cc->target_bitrate_bps = config->initial_bitrate_bps;

    cc->total_bytes_sent = 0;
    cc->total_packets_sent = 0;
    cc->last_update_time_ms = 0;
    cc->bytes_this_interval = 0;
    cc->pacing_next_send_time_ms = 0;
    cc->latest_rtt_ms = 0;
    cc->latest_fraction_lost = 0;

    TINYRTC_LOG_DEBUG("cc_create: created with initial bitrate %u bps",
        cc->target_bitrate_bps);

    return cc;
}

void tinyrtc_cc_destroy(tinyrtc_cc_t *cc)
{
    if (cc == NULL) {
        return;
    }
    tinyrtc_internal_free(cc);
    TINYRTC_LOG_DEBUG("cc_destroy: destroyed");
}

uint32_t tinyrtc_cc_get_estimated_bandwidth(tinyrtc_cc_t *cc)
{
    if (cc == NULL) {
        return 0;
    }
    return cc->estimated_bandwidth_bps;
}

uint32_t tinyrtc_cc_get_target_bitrate(tinyrtc_cc_t *cc)
{
    if (cc == NULL) {
        return 0;
    }
    return cc->target_bitrate_bps;
}

void tinyrtc_cc_report_packet_sent(
    tinyrtc_cc_t *cc,
    uint16_t sequence_number,
    uint32_t timestamp,
    uint64_t send_time_ms,
    size_t size_bytes)
{
    TINYRTC_CHECK_NULL(cc);

    TINYRTC_UNUSED(sequence_number);
    TINYRTC_UNUSED(timestamp);

    cc->total_bytes_sent += size_bytes;
    cc->total_packets_sent++;
    cc->bytes_this_interval += (uint32_t)size_bytes;
    cc->last_update_time_ms = send_time_ms;

    TINYRTC_LOG_DEBUG("cc_report_packet_sent: seq=%u size=%zu total=%llu",
        sequence_number, size_bytes, (unsigned long long)cc->total_bytes_sent);
}

void tinyrtc_cc_report_feedback(
    tinyrtc_cc_t *cc,
    uint16_t sequence_number,
    uint32_t timestamp,
    uint64_t receive_time_ms,
    uint32_t rtt_ms,
    uint8_t fraction_lost,
    uint64_t total_bytes,
    uint32_t total_packets)
{
    TINYRTC_CHECK_NULL(cc);
    TINYRTC_UNUSED(sequence_number);
    TINYRTC_UNUSED(timestamp);
    TINYRTC_UNUSED(receive_time_ms);
    TINYRTC_UNUSED(total_bytes);
    TINYRTC_UNUSED(total_packets);

    /* Update latest feedback metrics */
    cc->latest_rtt_ms = (double)rtt_ms;
    cc->latest_fraction_lost = (double)fraction_lost / 256.0;

    /* Congestion detection: if loss > threshold, we have congestion */
    const double LOSS_THRESHOLD = 0.02; /* 2% */

    if (cc->latest_fraction_lost > LOSS_THRESHOLD) {
        /* Congestion detected - reduce rate aggressively */
        tinyrtc_cc_report_congestion(cc);
        TINYRTC_LOG_DEBUG("cc_report_feedback: congestion detected (loss=%.3f), new target=%u bps",
            cc->latest_fraction_lost, cc->target_bitrate_bps);
    } else {
        /* No congestion - increase rate gradually (Additive Increase) */
        uint32_t increase = TINYRTC_CC_ADD_INCREASE;

        /* Scale increase by RTT - larger RTT means slower increase */
        if (cc->latest_rtt_ms > 0) {
            /* Normalize to 100ms RTT */
            increase = (uint32_t)(increase * (100.0 / cc->latest_rtt_ms));
        }

        uint32_t new_target = cc->target_bitrate_bps + increase;
        if (new_target > cc->config.max_bitrate_bps) {
            new_target = cc->config.max_bitrate_bps;
        }

        /* Update bandwidth estimate with exponential moving average */
        double alpha = TINYRTC_CC_ESTIMATE_ALPHA;
        cc->estimated_bandwidth_bps = (uint32_t)(
            alpha * cc->estimated_bandwidth_bps +
            (1.0 - alpha) * new_target);

        cc->target_bitrate_bps = new_target;

        TINYRTC_LOG_DEBUG("cc_report_feedback: no congestion, increased to %u bps",
            cc->target_bitrate_bps);
    }

    /* Clamp to min/max */
    if (cc->target_bitrate_bps < cc->config.min_bitrate_bps) {
        cc->target_bitrate_bps = cc->config.min_bitrate_bps;
    }
    if (cc->target_bitrate_bps > cc->config.max_bitrate_bps) {
        cc->target_bitrate_bps = cc->config.max_bitrate_bps;
    }
}

bool tinyrtc_cc_can_send(
    tinyrtc_cc_t *cc,
    size_t packet_size_bytes,
    uint64_t now_ms)
{
    if (cc == NULL) {
        return true;
    }

    /* If pacing is disabled, we can always send immediately */
    if (!cc->config.enable_pacing) {
        return true;
    }

    /* If this is the first packet, we can send it */
    if (cc->pacing_next_send_time_ms == 0) {
        cc->pacing_next_send_time_ms = now_ms;
        cc->bytes_this_interval = 0;
        return true;
    }

    /* Check if we've already used our allotment for this time interval */
    if (now_ms < cc->pacing_next_send_time_ms) {
        return false;
    }

    /* We can send - update pacing state */
    /* Calculate how long we should wait after sending this packet based on current bitrate */
    double bits = (double)packet_size_bytes * 8.0;
    double bitrate_bps = (double)cc->target_bitrate_bps;
    if (bitrate_bps < 1.0) {
        bitrate_bps = 1.0;
    }
    uint32_t wait_ms = (uint32_t)(bits * 1000.0 / bitrate_bps);

    cc->pacing_next_send_time_ms = now_ms + wait_ms;
    cc->bytes_this_interval += (uint32_t)packet_size_bytes;

    return true;
}

uint32_t tinyrtc_cc_get_wait_time_ms(tinyrtc_cc_t *cc)
{
    if (cc == NULL || cc->pacing_next_send_time_ms == 0) {
        return 0;
    }

    /* Get current time - we don't have access to it from here, so just return
     * the interval based on current bitrate for an average packet.
     */
    /* Assume 1200 byte average packet */
    double bits = 1200.0 * 8.0;
    double bitrate_bps = (double)cc->target_bitrate_bps;
    if (bitrate_bps < 1.0) {
        bitrate_bps = 1.0;
    }
    return (uint32_t)(bits * 1000.0 / bitrate_bps);
}

void tinyrtc_cc_report_congestion(tinyrtc_cc_t *cc)
{
    if (cc == NULL) {
        return;
    }

    /* Multiplicative decrease: cut target bitrate in half */
    uint32_t new_target = (uint32_t)((double)cc->target_bitrate_bps * TINYRTC_CC_MULTIPLY_DECREASE);

    /* Don't go below minimum */
    if (new_target < cc->config.min_bitrate_bps) {
        new_target = cc->config.min_bitrate_bps;
    }

    /* Update the estimate too */
    double alpha = TINYRTC_CC_ESTIMATE_ALPHA;
    cc->estimated_bandwidth_bps = (uint32_t)(
        alpha * cc->estimated_bandwidth_bps +
        (1.0 - alpha) * new_target);

    cc->target_bitrate_bps = new_target;

    TINYRTC_LOG_DEBUG("cc_report_congestion: reduced bitrate to %u bps",
        cc->target_bitrate_bps);
}
