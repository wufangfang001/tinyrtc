/*
 * tests_main.c
 *
 * Main entry point for all unit tests
 */

#include "minunit.h"

// Global test counters
int tests_run = 0;
int tests_passed = 0;

// Forward declarations of all test functions
extern int test_rtp_header_parse(void);
extern int test_rtp_header_build(void);
extern int test_sdp_parse_basic(void);
extern int test_jitter_buffer_basic(void);
extern int test_stun_parse(void);

int main(void) {
    printf("=== TinyRTC Unit Tests ===\n\n");

    MINUNIT_RUN_TEST(test_rtp_header_parse);
    MINUNIT_RUN_TEST(test_rtp_header_build);
    MINUNIT_RUN_TEST(test_sdp_parse_basic);
    MINUNIT_RUN_TEST(test_jitter_buffer_basic);
    MINUNIT_RUN_TEST(test_stun_parse);

    MINUNIT_SUMMARY();
}
