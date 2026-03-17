/**
 * @file media_codec.c
 * @brief Media codec definitions and utility functions
 *
 * Implements functions to get codec information (default payload, name, clock rate).
 *
 * Copyright (c) 2026
 * Licensed under MIT License
 */

#include "common.h"
#include "tinyrtc/media_codec.h"

/* =============================================================================
 * Codec information table
 * ========================================================================== */

typedef struct {
    tinyrtc_codec_id_t id;
    const char *name;
    int default_payload;
    uint32_t clock_rate;
    int channels;
} codec_info_t;

static const codec_info_t codec_info_table[] = {
    /* Video codecs */
    { TINYRTC_CODEC_H264, "H264", 100, 90000, 1 },

    /* Audio codecs */
    { TINYRTC_CODEC_OPUS, "OPUS", 111, 48000, 2 },
    { TINYRTC_CODEC_PCMA, "PCMA", 8, 8000, 1 },
    { TINYRTC_CODEC_PCMU, "PCMU", 0, 8000, 1 },
};

/* =============================================================================
 * Local helper to find codec info
 * ========================================================================== */

static const codec_info_t *find_codec_info(tinyrtc_codec_id_t codec)
{
    for (size_t i = 0; i < TINYRTC_ARRAY_LEN(codec_info_table); i++) {
        if (codec_info_table[i].id == codec) {
            return &codec_info_table[i];
        }
    }
    return NULL;
}

/* =============================================================================
 * Public API implementation
 * ========================================================================== */

int tinyrtc_codec_get_default_payload(tinyrtc_codec_id_t codec)
{
    const codec_info_t *info = find_codec_info(codec);
    if (info == NULL) {
        return -1;
    }
    return info->default_payload;
}

const char *tinyrtc_codec_get_name(tinyrtc_codec_id_t codec)
{
    const codec_info_t *info = find_codec_info(codec);
    if (info == NULL) {
        return "UNKNOWN";
    }
    return info->name;
}

uint32_t tinyrtc_codec_get_clock_rate(tinyrtc_codec_id_t codec)
{
    const codec_info_t *info = find_codec_info(codec);
    if (info == NULL) {
        return 0;
    }
    return info->clock_rate;
}

int tinyrtc_codec_get_channels(tinyrtc_codec_id_t codec)
{
    const codec_info_t *info = find_codec_info(codec);
    if (info == NULL) {
        return 1;
    }
    return info->channels;
}
