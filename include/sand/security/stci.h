#ifndef SAND_SECURITY_STCI_H
#define SAND_SECURITY_STCI_H

#include <stdint.h>

/*
 * SAND Trust & Code Integrity (STCI).
 *
 * This header defines the kernel-facing verification contract. The Ed25519
 * signature primitive and the SAPP v2 transcript check are implemented in
 * kernel/security/stci.c on top of kernel/crypto. Certificate-chain
 * validation, the trust store and the launch gate are NOT implemented yet,
 * so this MUST NOT be treated as enabling signed application execution.
 */

#define SAND_STCI_ABI_MAJOR 1u
#define SAND_STCI_ABI_MINOR 0u
#define SAND_STCI_MAX_ROOTS 8u
#define SAND_STCI_ED25519_PUBLIC_KEY_SIZE 32u
#define SAND_STCI_ED25519_SIGNATURE_SIZE 64u
#define SAND_STCI_SHA256_SIZE 32u

#define SAND_STCI_SAPP_V2_DOMAIN "SAND-SAPP-V2-ED25519"
#define SAND_STCI_SAPP_V2_DOMAIN_SIZE 21u /* includes the terminating NUL */

enum sand_stci_status {
    SAND_STCI_OK = 0,
    SAND_STCI_ERR_ARGUMENT = -1,
    SAND_STCI_ERR_FORMAT = -2,
    SAND_STCI_ERR_UNSUPPORTED = -3,
    SAND_STCI_ERR_UNTRUSTED_ROOT = -4,
    SAND_STCI_ERR_CERTIFICATE = -5,
    SAND_STCI_ERR_EXPIRED = -6,
    SAND_STCI_ERR_REVOKED = -7,
    SAND_STCI_ERR_SIGNATURE = -8,
    SAND_STCI_ERR_POLICY = -9,
    SAND_STCI_ERR_NO_MEMORY = -10
};

enum sand_stci_policy_flags {
    /* Production policy: unsigned SAPP v1 is not executable. */
    SAND_STCI_POLICY_REQUIRE_SIGNED = 0x00000001u,
    /* Reserved for a future explicit, visible, auditable developer profile. */
    SAND_STCI_POLICY_DEVELOPER_MODE = 0x00000002u,
    /* Require a separate SAND release-certification attestation. */
    SAND_STCI_POLICY_REQUIRE_ATTESTATION = 0x00000004u
};

struct sand_stci_root {
    uint8_t key_id[SAND_STCI_SHA256_SIZE];
    uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE];
    uint32_t flags;
};

struct sand_stci_trust_store {
    uint32_t abi_major;
    uint32_t struct_size;
    uint32_t root_count;
    uint32_t generation;
    const struct sand_stci_root *roots;
};

struct sand_stci_policy {
    uint32_t abi_major;
    uint32_t struct_size;
    uint32_t flags;
    uint32_t reserved;
};

/*
 * Verify one immutable SAPP v2 image.
 *
 * image must refer to a kernel-owned immutable snapshot. The verifier must
 * validate all lengths/offsets before dereferencing and must not retain image.
 * No executable mapping, relocation or process creation may happen before
 * this function returns SAND_STCI_OK.
 *
 * The implementation must validate the publisher certificate chain against
 * the provisioned trust store and verify the exact transcript documented in
 * sand-apps/FORMAT.md. It must not trust a root carried by the image.
 *
 * Current status: declaration only (needs certificate chain + trust store).
 */
int sand_stci_verify_sapp_v2(
    const void *image,
    uint32_t image_size,
    const struct sand_stci_trust_store *trust_store,
    const struct sand_stci_policy *policy,
    uint8_t image_digest_out[SAND_STCI_SHA256_SIZE]);

/* Ed25519 verification primitive (in-house, kernel/crypto/ed25519.c).
 * Signature is 64 bytes; public_key is 32 bytes. Returns 1 only on success.
 * Public data only: no secret-dependent timing property is claimed.
 */
int sand_stci_ed25519_verify(
    const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
    const uint8_t *message,
    uint32_t message_size,
    const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]);

/* Largest image the transcript verifier accepts (fail closed above this). */
#define SAND_STCI_MAX_IMAGE_SIZE (16u * 1024u * 1024u)

/*
 * Verify the SAPP v2 Ed25519 signature of an image against ONE already
 * trusted public key (the publisher key from a verified certificate).
 * Transcript: "SAND-SAPP-V2-ED25519\0" || image[0, signature_offset).
 * Returns SAND_STCI_OK only on a valid signature; null arguments give
 * ERR_ARGUMENT, signature_offset == 0 / > image_size / oversize images give
 * ERR_FORMAT, and a bad signature gives ERR_SIGNATURE (fail closed).
 */
int sand_stci_verify_sapp_v2_signature(
    const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
    const void *image,
    uint32_t image_size,
    uint32_t signature_offset,
    const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]);

#endif
