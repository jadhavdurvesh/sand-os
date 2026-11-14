#ifndef SAND_CRYPTO_SHA512_H
#define SAND_CRYPTO_SHA512_H
#include <stddef.h>
#include <stdint.h>

/* SAND in-house SHA-512 (FIPS 180-4). Freestanding: no libc, no libgcc. */
#define SAND_SHA512_DIGEST_SIZE 64u

struct sand_sha512 {
    uint64_t h[8];
    uint64_t total;
    uint8_t buf[128];
    uint32_t buflen;
};

void sand_sha512_init(struct sand_sha512 *c);
void sand_sha512_update(struct sand_sha512 *c, const void *data, size_t len);
void sand_sha512_final(struct sand_sha512 *c, uint8_t out[SAND_SHA512_DIGEST_SIZE]);

#endif
