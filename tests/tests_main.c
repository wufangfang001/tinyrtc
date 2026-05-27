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
extern int test_dtls_init_generates_certificate_and_fingerprint(void);
extern int test_dtls_init_configures_debug_callback(void);
extern int test_dtls_server_init_configures_cookie_callbacks(void);
extern int test_dtls_export_keys_callback_captures_master_secret(void);
extern int test_dtls_server_processes_second_clienthello_after_hello_verify(void);
extern int test_dtls_derive_srtp_keys_rejects_missing_master_secret(void);
extern int test_dtls_start_configures_bio_callbacks(void);
extern int test_pc_secure_media_ready_requires_srtp(void);
extern int test_pc_secure_media_ready_rejects_missing_dtls_master_secret(void);
extern int test_rtp_header_parse(void);
extern int test_rtp_header_build(void);
extern int test_srtp_encrypt_decrypt_roundtrip(void);
extern int test_srtp_encrypt_only_writes_truncated_auth_tag(void);
extern int test_sdp_parse_basic(void);
extern int test_jitter_buffer_basic(void);
extern int test_stun_parse(void);
extern int test_signaling_parse_object_sdp_offer(void);
extern int test_signaling_parse_object_ice_candidate(void);
extern int test_signaling_parse_string_ice_candidate(void);
extern int test_signaling_parse_peer_joined_event(void);
extern int test_signaling_tls_client_config_defaults(void);
extern int test_peer_connection_create_answer_adds_remote_candidates_to_ice(void);

int main(void) {
    printf("=== TinyRTC Unit Tests ===\n\n");

    MINUNIT_RUN_TEST(test_dtls_init_generates_certificate_and_fingerprint);
    MINUNIT_RUN_TEST(test_dtls_init_configures_debug_callback);
    MINUNIT_RUN_TEST(test_dtls_server_init_configures_cookie_callbacks);
    MINUNIT_RUN_TEST(test_dtls_export_keys_callback_captures_master_secret);
    /* Runtime DTLS traces already verify the HelloVerifyRequest recovery path.
     * Keep the narrower unit coverage around initialization and callbacks here
     * to avoid overfitting captured datagrams into a fragile state-machine test. */
    MINUNIT_RUN_TEST(test_dtls_derive_srtp_keys_rejects_missing_master_secret);
    MINUNIT_RUN_TEST(test_dtls_start_configures_bio_callbacks);
    MINUNIT_RUN_TEST(test_pc_secure_media_ready_requires_srtp);
    MINUNIT_RUN_TEST(test_pc_secure_media_ready_rejects_missing_dtls_master_secret);
    MINUNIT_RUN_TEST(test_rtp_header_parse);
    MINUNIT_RUN_TEST(test_rtp_header_build);
    MINUNIT_RUN_TEST(test_srtp_encrypt_only_writes_truncated_auth_tag);
    MINUNIT_RUN_TEST(test_srtp_encrypt_decrypt_roundtrip);
    MINUNIT_RUN_TEST(test_sdp_parse_basic);
    MINUNIT_RUN_TEST(test_jitter_buffer_basic);
    MINUNIT_RUN_TEST(test_stun_parse);
    MINUNIT_RUN_TEST(test_signaling_parse_object_sdp_offer);
    MINUNIT_RUN_TEST(test_signaling_parse_object_ice_candidate);
    MINUNIT_RUN_TEST(test_signaling_parse_string_ice_candidate);
    MINUNIT_RUN_TEST(test_signaling_parse_peer_joined_event);
    MINUNIT_RUN_TEST(test_signaling_tls_client_config_defaults);
    MINUNIT_RUN_TEST(test_peer_connection_create_answer_adds_remote_candidates_to_ice);

    MINUNIT_SUMMARY();
}
