#ifndef SAND_CRYPTO_ED25519_H
#define SAND_CRYPTO_ED25519_H
#include <stddef.h>
#include <stdint.h>

/*
 * SAND in-house Ed25519 signature VERIFICATION (RFC 8032, PureEdDSA).
 *
 * Verification handles only public data, so no secret-dependent timing
 * property is claimed. Strict checks: non-canonical S (>= L) and
 * non-canonical or off-curve public keys / R encodings are rejected.
 * The message is supplied in two parts so a domain-separation prefix can be
 * hashed without copying a multi-hundred-KiB image.
 *
 * Stack use is roughly 3 KiB; do not call from a tiny thread stack.
 */
#define SAND_ED25519_PUBLIC_KEY_SIZE 32u
#define SAND_ED25519_SIGNATURE_SIZE 64u

/* Returns 1 when the signature is valid, 0 otherwise (including bad args). */
int sand_ed25519_verify2(const uint8_t sig[SAND_ED25519_SIGNATURE_SIZE],
                         const uint8_t pub[SAND_ED25519_PUBLIC_KEY_SIZE],
                         const void *part1, size_t len1,
                         const void *part2, size_t len2);

int sand_ed25519_verify(const uint8_t sig[SAND_ED25519_SIGNATURE_SIZE],
                        const uint8_t pub[SAND_ED25519_PUBLIC_KEY_SIZE],
                        const void *msg, size_t len);

#endif
