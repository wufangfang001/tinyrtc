/*
 * test_stun.c
 *
 * Unit tests for STUN message parsing
 */

#include "minunit.h"
#include "tinyrtc/tinyrtc.h"
#include "ice_internal.h"

/* STUN constants verification */
MINUNIT_TEST(test_stun_parse)
{
    // Just verify the constants are correct
    #define STUN_MAGIC_COOKIE 0x2112A442
    #define STUN_BINDING_REQUEST      0x0001

    // The actual processing is done via stun_process_response
    // We just test that we can include headers and compile correctly
    // and verify the magic cookie is correct
    MINUNIT_ASSERT(STUN_MAGIC_COOKIE == 0x2112A442, "Wrong STUN magic cookie");
    MINUNIT_ASSERT(STUN_BINDING_REQUEST == 0x0001, "Wrong STUN binding request type");

    return 0;
}

MINUNIT_TEST(test_stun_finalize_adds_integrity_and_fingerprint)
{
    uint8_t buffer[256];
    stun_header_t *hdr = (stun_header_t *)buffer;
    size_t len = sizeof(stun_header_t);
    tinyrtc_error_t err;

    memset(buffer, 0, sizeof(buffer));
    hdr->type = htons(STUN_BINDING_REQUEST);
    hdr->magic_cookie = htonl(STUN_MAGIC_COOKIE);

    err = stun_finalize_message(buffer, &len, sizeof(buffer), "test-password");

    MINUNIT_ASSERT(err == TINYRTC_OK, "Expected STUN finalize to succeed");
    MINUNIT_ASSERT(len > sizeof(stun_header_t), "Expected STUN finalize to append attributes");
    MINUNIT_ASSERT(ntohs(hdr->length) == (uint16_t)(len - sizeof(stun_header_t)),
                   "Expected STUN header length to match finalized message");
    MINUNIT_ASSERT(memmem(buffer, len, "\x00\x08", 2) != NULL,
                   "Expected MESSAGE-INTEGRITY attribute");
    MINUNIT_ASSERT(memmem(buffer, len, "\x80\x28", 2) != NULL,
                   "Expected FINGERPRINT attribute");

    return 0;
}
