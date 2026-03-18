/*
 * test_jitter_buffer.c
 *
 * Unit tests for jitter buffer
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "media.h"

/* Test basic jitter buffer operations */
MINUNIT_TEST(test_jitter_buffer_basic)
{
    tinyrtc_jitter_config_t config;
    tinyrtc_jitter_get_default_config(&config);
    tinyrtc_jitter_buffer_t *jb = tinyrtc_jitter_buffer_create(&config);
    MINUNIT_ASSERT(jb != NULL, "Failed to create jitter buffer");

    // Check we can get delay
    uint32_t delay = tinyrtc_jitter_buffer_get_delay(jb);
    (void)delay;

    // Just verify API works, the actual packet buffering needs more context
    // (needs RTP header parsed etc.)
    tinyrtc_jitter_buffer_destroy(jb);
    return 0;
}
