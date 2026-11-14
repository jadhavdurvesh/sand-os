#ifndef SAND_CRYPTO_SHA256_H
#define SAND_CRYPTO_SHA256_H
#include <stddef.h>
#include <stdint.h>

/* SAND in-house SHA-256 (FIPS 180-4). Freestanding. */
#define SAND_SHA256_DIGEST_SIZE 32u
struct sand_sha256 { uint32_t h[8]; uint64_t total; uint8_t buf[64]; uint32_t buflen; };
void sand_sha256_init(struct sand_sha256 *c);
void sand_sha256_update(struct sand_sha256 *c, const void *data, size_t len);
void sand_sha256_final(struct sand_sha256 *c, uint8_t out[SAND_SHA256_DIGEST_SIZE]);
void sand_sha256(const void *data, size_t len, uint8_t out[SAND_SHA256_DIGEST_SIZE]);
#endif
