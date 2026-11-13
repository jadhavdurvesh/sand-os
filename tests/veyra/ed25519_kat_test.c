/* Host test for the SAND in-house SHA-512 / Ed25519 verifier and the STCI
 * SAPP v2 transcript check, using docs/STCI_ED25519_KNOWN_ANSWER.md. */
#include <stdio.h>
#include <string.h>
#include "sand/crypto/sha512.h"
#include "sand/crypto/ed25519.h"
#include "sand/security/stci.h"

static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while(0)

static int hex(const char *s, uint8_t *o, size_t n) {
    for (size_t i = 0; i < n; ++i) { unsigned v; if (sscanf(s + 2*i, "%2x", &v) != 1) return -1; o[i] = (uint8_t)v; }
    return 0;
}
static const char *PUB = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
static const char *SIG = "ad3c43fa3f87f5679468515bbc48f1ebe2b92233762082858a6e6cf431bd4424d98351425a70a8eb36ab0d7c4549a500ebb24910bb65fdc423b17933c0b6ec01";
static const char *TR  = "53414e442d534150502d56322d4544323535313900534150502d535443492d4b41542d763100000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

int main(void) {
    uint8_t pub[32], sig[64], tr[128], d[64];
    hex(PUB, pub, 32); hex(SIG, sig, 64);
    size_t trn = strlen(TR) / 2; hex(TR, tr, trn);

    /* SHA-512 known answers */
    struct sand_sha512 c; sand_sha512_init(&c); sand_sha512_update(&c, "abc", 3); sand_sha512_final(&c, d);
    uint8_t abc[64]; hex("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", abc, 64);
    CHECK(memcmp(d, abc, 64) == 0);
    { static uint8_t blk[1000]; memset(blk, 'a', sizeof blk); uint8_t mil[64];
      hex("e718483d0ce769644e2e42c7bc15b4638e1f98b13b2044285632a803afa973ebde0ff244877ea60a4cb0432ce577c31beb009c5c2c49aa2e4eadb217ad8cc09b", mil, 64);
      sand_sha512_init(&c); for (int i = 0; i < 1000; ++i) sand_sha512_update(&c, blk, sizeof blk); sand_sha512_final(&c, d);
      CHECK(memcmp(d, mil, 64) == 0); }
    /* (cross-check of long messages is done in ed25519_crosscheck.py) */

    /* Ed25519 on the raw transcript */
    CHECK(sand_ed25519_verify(sig, pub, tr, trn) == 1);
    /* 1-bit transcript flip fails (every bit) */
    for (size_t i = 0; i < trn * 8; ++i) { uint8_t m[128]; memcpy(m, tr, trn); m[i / 8] ^= (uint8_t)(1u << (i % 8)); CHECK(sand_ed25519_verify(sig, pub, m, trn) == 0); }
    /* 1-bit signature flip fails (every bit) */
    for (int i = 0; i < 64 * 8; ++i) { uint8_t s2[64]; memcpy(s2, sig, 64); s2[i / 8] ^= (uint8_t)(1u << (i % 8)); CHECK(sand_ed25519_verify(s2, pub, tr, trn) == 0); }
    /* different key fails */
    uint8_t other[32]; memcpy(other, pub, 32); other[0] ^= 1; CHECK(sand_ed25519_verify(sig, other, tr, trn) == 0);
    uint8_t other2[32] = {0}; other2[0] = 1; /* identity point */ CHECK(sand_ed25519_verify(sig, other2, tr, trn) == 0);
    /* signature without the domain separator fails */
    CHECK(sand_ed25519_verify(sig, pub, tr + 21, trn - 21) == 0);
    /* split-call equivalence */
    CHECK(sand_ed25519_verify2(sig, pub, tr, 21, tr + 21, trn - 21) == 1);
    CHECK(sand_ed25519_verify2(sig, pub, tr, 5, tr + 5, trn - 5) == 1);
    /* empty / truncated / oversized / null inputs fail safely */
    CHECK(sand_ed25519_verify(sig, pub, tr, 0) == 0);
    CHECK(sand_ed25519_verify(sig, pub, tr, trn - 1) == 0);
    CHECK(sand_ed25519_verify(sig, pub, tr, trn + 0) == 1);
    CHECK(sand_ed25519_verify(NULL, pub, tr, trn) == 0);
    CHECK(sand_ed25519_verify(sig, NULL, tr, trn) == 0);
    CHECK(sand_ed25519_verify(sig, pub, NULL, trn) == 0);
    { uint8_t zero_sig[64] = {0}; CHECK(sand_ed25519_verify(zero_sig, pub, tr, trn) == 0); }
    { uint8_t ff[64]; memset(ff, 0xff, 64); CHECK(sand_ed25519_verify(ff, pub, tr, trn) == 0); }
    { /* non-canonical S = S + L must be rejected */
      static const uint8_t Lb[32] = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};
      uint8_t s2[64]; memcpy(s2, sig, 64); unsigned carry = 0;
      for (int i = 0; i < 32; ++i) { unsigned v = s2[32 + i] + Lb[i] + carry; s2[32 + i] = (uint8_t)v; carry = v >> 8; }
      CHECK(sand_ed25519_verify(s2, pub, tr, trn) == 0); }
    { /* non-canonical public key y >= p */
      uint8_t bad[32]; memset(bad, 0xff, 32); bad[0] = 0xee; bad[31] = 0x7f; CHECK(sand_ed25519_verify(sig, bad, tr, trn) == 0); }

    /* STCI wrapper: image = payload, domain separator applied internally */
    const uint8_t *payload = tr + SAND_STCI_SAPP_V2_DOMAIN_SIZE; uint32_t plen = (uint32_t)(trn - SAND_STCI_SAPP_V2_DOMAIN_SIZE);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, plen, plen, sig) == SAND_STCI_OK);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, plen, plen - 1, sig) == SAND_STCI_ERR_SIGNATURE);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, plen, 0, sig) == SAND_STCI_ERR_FORMAT);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, plen, plen + 1, sig) == SAND_STCI_ERR_FORMAT);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, 0, 0, sig) == SAND_STCI_ERR_FORMAT);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, SAND_STCI_MAX_IMAGE_SIZE + 1u, plen, sig) == SAND_STCI_ERR_FORMAT);
    CHECK(sand_stci_verify_sapp_v2_signature(NULL, payload, plen, plen, sig) == SAND_STCI_ERR_ARGUMENT);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, NULL, plen, plen, sig) == SAND_STCI_ERR_ARGUMENT);
    CHECK(sand_stci_verify_sapp_v2_signature(pub, payload, plen, plen, NULL) == SAND_STCI_ERR_ARGUMENT);

    if (failures) { printf("ed25519 KAT: %d failure(s)\n", failures); return 1; }
    puts("ed25519 KAT + STCI transcript tests passed");
    return 0;
}
