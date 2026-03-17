/**
 * @file media_codec.h
 * @brief Media codec definitions and frame-level media API
 */

#ifndef TINYRTC_MEDIA_CODEC_H
#define TINYRTC_MEDIA_CODEC_H

#include "tinyrtc/tinyrtc.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Supported codec types
 */
typedef enum {
    /* Video codecs */
    TINYRTC_CODEC_H264 = 0,

    /* Audio codecs */
    TINYRTC_CODEC_OPUS = 100,
    TINYRTC_CODEC_PCMA = 101,
    TINYRTC_CODEC_PCMU = 102,
} tinyrtc_codec_id_t;

/**
 * @brief Get default payload type for a codec
 *
 * @param codec Codec ID
 * @return Default RTP payload type number
 */
int tinyrtc_codec_get_default_payload(tinyrtc_codec_id_t codec);

/**
 * @brief Get codec name string
 *
 * @param codec Codec ID
 * @return Codec name (e.g., "H264", "OPUS")
 */
const char *tinyrtc_codec_get_name(tinyrtc_codec_id_t codec);

/**
 * @brief Get clock rate for a codec
 *
 * @param codec Codec ID
 * @return RTP clock rate in Hz
 */
uint32_t tinyrtc_codec_get_clock_rate(tinyrtc_codec_id_t codec);

/**
 * @brief Get number of channels for a codec
 *
 * @param codec Codec ID
 * @return Number of audio channels (1=mono, 2=stereo)
 */
int tinyrtc_codec_get_channels(tinyrtc_codec_id_t codec);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_MEDIA_CODEC_H */
