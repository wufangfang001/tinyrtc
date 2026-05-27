/*
 * test_peer_connection.c
 *
 * Unit tests for internal PeerConnection media send readiness.
 */

#include "minunit.h"
#include "peer_connection_internal.h"

MINUNIT_TEST(test_pc_secure_media_ready_requires_srtp)
{
    tinyrtc_peer_connection_t pc;
    ice_session_t ice;
    ice_check_pair_t pair;
    ice_candidate_internal_t local;
    ice_candidate_internal_t remote;

    memset(&pc, 0, sizeof(pc));
    memset(&ice, 0, sizeof(ice));
    memset(&pair, 0, sizeof(pair));
    memset(&local, 0, sizeof(local));
    memset(&remote, 0, sizeof(remote));

    pair.local = &local;
    pair.remote = &remote;
    pair.succeeded = true;

    ice.socket = 7;
    ice.selected_pair = &pair;
    pc.ice = &ice;

    MINUNIT_ASSERT(!pc_is_secure_media_ready(&pc),
                   "Expected media send to stay blocked before SRTP is initialized");

    pc.srtp_initialized = true;

    MINUNIT_ASSERT(pc_is_secure_media_ready(&pc),
                   "Expected media send to become ready after SRTP initialization");

    return 0;
}

MINUNIT_TEST(test_pc_secure_media_ready_rejects_missing_dtls_master_secret)
{
    tinyrtc_peer_connection_t pc;
    dtls_context_t *dtls;
    uint8_t client_key[16];
    uint8_t client_salt[14];
    uint8_t server_key[16];
    uint8_t server_salt[14];
    tinyrtc_error_t err;

    memset(&pc, 0, sizeof(pc));

    dtls = dtls_init(DTLS_ROLE_CLIENT);
    MINUNIT_ASSERT(dtls != NULL, "Expected DTLS init to succeed");
    dtls->handshake_complete = true;
    if (dtls->ssl != NULL) {
        dtls->ssl->state = MBEDTLS_SSL_HANDSHAKE_OVER;
    }
    pc.dtls = dtls;

    err = dtls_derive_srtp_keys(dtls, client_key, client_salt, server_key, server_salt);
    MINUNIT_ASSERT(err == TINYRTC_ERROR_INVALID_STATE,
                   "Expected SRTP key derivation to fail when master secret was not captured");
    MINUNIT_ASSERT(!pc.srtp_initialized,
                   "Expected peer connection SRTP state to remain false after derivation failure");
    MINUNIT_ASSERT(pc.srtp == NULL,
                   "Expected peer connection SRTP context to remain NULL after derivation failure");

    dtls_destroy(dtls);
    return 0;
}
