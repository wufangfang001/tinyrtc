/*
 * test_srtp.c
 *
 * Unit tests for SRTP packet protection.
 */

#include "minunit.h"
#include "dtls_srtp_internal.h"

MINUNIT_TEST(test_srtp_encrypt_decrypt_roundtrip)
{
    uint8_t key[16];
    uint8_t salt[14];
    uint8_t packet[128];
    uint8_t original[128];
    size_t len = 20;
    size_t output_len = 0;
    srtp_context_t *srtp;

    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < sizeof(salt); i++) {
        salt[i] = (uint8_t)(0x10 + i);
    }

    memset(packet, 0, sizeof(packet));
    packet[0] = 0x80; /* RTP v2 */
    packet[1] = 96;   /* PT */
    packet[2] = 0x12;
    packet[3] = 0x34;
    packet[4] = 0x56;
    packet[5] = 0x78;
    packet[6] = 0x9a;
    packet[7] = 0xbc;
    packet[8] = 0x11;
    packet[9] = 0x22;
    packet[10] = 0x33;
    packet[11] = 0x44; /* SSRC */
    packet[12] = 0xde;
    packet[13] = 0xad;
    packet[14] = 0xbe;
    packet[15] = 0xef;
    packet[16] = 0xca;
    packet[17] = 0xfe;
    packet[18] = 0xba;
    packet[19] = 0xbe;

    memcpy(original, packet, len);

    srtp = srtp_init(key, salt);
    MINUNIT_ASSERT(srtp != NULL, "Expected SRTP context init to succeed");
    printf("debug srtp: init ok\n");

    MINUNIT_ASSERT(srtp_encrypt_packet(srtp, packet, &len, sizeof(packet)) == TINYRTC_OK,
                   "Expected SRTP packet encryption to succeed");
    printf("debug srtp: encrypt ok len=%zu\n", len);
    MINUNIT_ASSERT(len == 30, "Expected SRTP auth tag to extend packet by 10 bytes");

    MINUNIT_ASSERT(srtp_decrypt_packet(srtp, packet, len, &output_len) == TINYRTC_OK,
                   "Expected SRTP packet decryption to succeed");
    printf("debug srtp: decrypt ok out=%zu\n", output_len);
    MINUNIT_ASSERT(output_len == sizeof(original) - (sizeof(original) - 20),
                   "Expected decrypted packet length to match original RTP length");
    MINUNIT_ASSERT(memcmp(packet, original, output_len) == 0,
                   "Expected decrypted RTP packet to match original contents");

    srtp_destroy(srtp);
    return 0;
}

MINUNIT_TEST(test_srtp_encrypt_only_writes_truncated_auth_tag)
{
    uint8_t key[16];
    uint8_t salt[14];
    uint8_t packet[64];
    size_t len = 20;
    srtp_context_t *srtp;

    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < sizeof(salt); i++) {
        salt[i] = (uint8_t)(0x10 + i);
    }

    memset(packet, 0xAA, sizeof(packet));
    packet[0] = 0x80;
    packet[1] = 96;
    packet[2] = 0x12;
    packet[3] = 0x34;
    packet[4] = 0x56;
    packet[5] = 0x78;
    packet[6] = 0x9a;
    packet[7] = 0xbc;
    packet[8] = 0x11;
    packet[9] = 0x22;
    packet[10] = 0x33;
    packet[11] = 0x44;
    packet[12] = 0xde;
    packet[13] = 0xad;
    packet[14] = 0xbe;
    packet[15] = 0xef;
    packet[16] = 0xca;
    packet[17] = 0xfe;
    packet[18] = 0xba;
    packet[19] = 0xbe;

    srtp = srtp_init(key, salt);
    MINUNIT_ASSERT(srtp != NULL, "Expected SRTP context init to succeed");

    MINUNIT_ASSERT(srtp_encrypt_packet(srtp, packet, &len, sizeof(packet)) == TINYRTC_OK,
                   "Expected SRTP packet encryption to succeed");
    MINUNIT_ASSERT(len == 30, "Expected SRTP auth tag to extend packet by 10 bytes");

    for (size_t i = len; i < len + 10; i++) {
        MINUNIT_ASSERT(packet[i] == 0xAA,
                       "Expected encryption to leave bytes beyond 10-byte auth tag untouched");
    }

    srtp_destroy(srtp);
    return 0;
}
