/**
 * @file common.h
 * @brief Common utilities for demo applications
 */

#ifndef TINYRTC_DEMO_COMMON_H
#define TINYRTC_DEMO_COMMON_H

#include "tinyrtc/tinyrtc.h"
#include "tinyrtc/peer_connection.h"
#include "api/aosl.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read SDP from file
 *
 * @param path File path
 * @return Allocated SDP string, must be freed by caller
 */
char *demo_read_sdp(const char *path);

/**
 * @brief Write SDP to file
 *
 * @param path File path
 * @param sdp SDP string
 * @return 0 on success, negative on error
 */
int demo_write_sdp(const char *path, const char *sdp);

typedef struct {
    uint8_t *data;
    size_t len;
} demo_h264_access_unit_t;

typedef struct {
    demo_h264_access_unit_t *units;
    size_t count;
    size_t current_index;
} demo_h264_stream_t;

int demo_h264_stream_load(const char *path, demo_h264_stream_t *stream);
void demo_h264_stream_reset(demo_h264_stream_t *stream);
void demo_h264_stream_free(demo_h264_stream_t *stream);

/**
 * @brief Initialize AOSL for demo
 *
 * @return 0 on success
 */
int demo_init_aosl(void);

/**
 * @brief Exit AOSL for demo
 */
void demo_exit_aosl(void);

#ifdef __cplusplus
}
#endif

#endif /* TINYRTC_DEMO_COMMON_H */
