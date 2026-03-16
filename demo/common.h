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
