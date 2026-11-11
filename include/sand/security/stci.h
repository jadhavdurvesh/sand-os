#ifndef SAND_SECURITY_STCI_H
#define SAND_SECURITY_STCI_H

#include <stdint.h>

/*
 * SAND Trust & Code Integrity (STCI).
 *
 * This header defines the kernel-facing verification contract only.
 * It deliberately contains no cryptographic implementation and MUST NOT
 * be treated as enabling signed application execution.
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
 * Current status: declaration only. No implementation is linked into SAND.
 */
int sand_stci_verify_sapp_v2(
    const void *image,
    uint32_t image_size,
    const struct sand_stci_trust_store *trust_store,
    const struct sand_stci_policy *policy,
    uint8_t image_digest_out[SAND_STCI_SHA256_SIZE]);

/* Constant-time Ed25519 verification primitive required by the verifier.
 * Signature is 64 bytes; public_key is 32 bytes. Returns 1 only on success.
 * A freestanding, reviewed implementation is not yet selected or linked.
 */
int sand_stci_ed25519_verify(
    const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
    const uint8_t *message,
    uint32_t message_size,
    const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]);

#endif
