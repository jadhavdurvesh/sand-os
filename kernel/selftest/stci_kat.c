#include "sand/crypto/sha512.h"
#include "sand/crypto/ed25519.h"
#include "sand/security/stci.h"

/* Boot-time known-answer self-test for the in-house verifier
 * (vector: docs/STCI_ED25519_KNOWN_ANSWER.md). Returns 0 on success. */
static const uint8_t PUB[32] = {0xd7,0x5a,0x98,0x01,0x82,0xb1,0x0a,0xb7,0xd5,0x4b,0xfe,0xd3,0xc9,0x64,0x07,0x3a,
    0x0e,0xe1,0x72,0xf3,0xda,0xa6,0x23,0x25,0xaf,0x02,0x1a,0x68,0xf7,0x07,0x51,0x1a};
static const uint8_t SIG[64] = {0xad, 0x3c, 0x43, 0xfa, 0x3f, 0x87, 0xf5, 0x67, 0x94, 0x68, 0x51, 0x5b, 0xbc, 0x48, 0xf1, 0xeb,
    0xe2, 0xb9, 0x22, 0x33, 0x76, 0x20, 0x82, 0x85, 0x8a, 0x6e, 0x6c, 0xf4, 0x31, 0xbd, 0x44, 0x24,
    0xd9, 0x83, 0x51, 0x42, 0x5a, 0x70, 0xa8, 0xeb, 0x36, 0xab, 0x0d, 0x7c, 0x45, 0x49, 0xa5, 0x00,
    0xeb, 0xb2, 0x49, 0x10, 0xbb, 0x65, 0xfd, 0xc4, 0x23, 0xb1, 0x79, 0x33, 0xc0, 0xb6, 0xec, 0x01};
static const uint8_t PAYLOAD[49] = {'S','A','P','P','-','S','T','C','I','-','K','A','T','-','v','1',0,
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};

int sand_stci_selftest(void) {
    uint8_t sig[64], pub[32], payload[49];
    for (int i = 0; i < 64; ++i) sig[i] = SIG[i];
    for (int i = 0; i < 32; ++i) pub[i] = PUB[i];
    for (int i = 0; i < 49; ++i) payload[i] = PAYLOAD[i];
    if (sand_stci_verify_sapp_v2_signature(pub, payload, 49, 49, sig) != SAND_STCI_OK) return 1;
    payload[20] ^= 1;
    if (sand_stci_verify_sapp_v2_signature(pub, payload, 49, 49, sig) != SAND_STCI_ERR_SIGNATURE) return 2;
    payload[20] ^= 1; sig[10] ^= 1;
    if (sand_stci_verify_sapp_v2_signature(pub, payload, 49, 49, sig) != SAND_STCI_ERR_SIGNATURE) return 3;
    sig[10] ^= 1; pub[5] ^= 1;
    if (sand_stci_verify_sapp_v2_signature(pub, payload, 49, 49, sig) == SAND_STCI_OK) return 4;
    pub[5] ^= 1;
    if (sand_stci_verify_sapp_v2_signature(pub, payload, 49, 0, sig) != SAND_STCI_ERR_FORMAT) return 5;
    if (sand_stci_verify_sapp_v2_signature(pub, 0, 49, 49, sig) != SAND_STCI_ERR_ARGUMENT) return 6;
    return 0;
}
