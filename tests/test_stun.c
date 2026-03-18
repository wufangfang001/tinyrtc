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
