#include "sand/security/stci.h"
#include "sand/crypto/ed25519.h"

int sand_stci_ed25519_verify(const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
                             const uint8_t *message, uint32_t message_size,
                             const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]) {
    if (!public_key || !signature || (message_size && !message)) return 0;
    return sand_ed25519_verify(signature, public_key, message, message_size);
}

int sand_stci_verify_sapp_v2_signature(const uint8_t public_key[SAND_STCI_ED25519_PUBLIC_KEY_SIZE],
                                       const void *image, uint32_t image_size, uint32_t signature_offset,
                                       const uint8_t signature[SAND_STCI_ED25519_SIGNATURE_SIZE]) {
    if (!public_key || !image || !signature) return SAND_STCI_ERR_ARGUMENT;
    if (image_size == 0u || image_size > SAND_STCI_MAX_IMAGE_SIZE) return SAND_STCI_ERR_FORMAT;
    if (signature_offset == 0u || signature_offset > image_size) return SAND_STCI_ERR_FORMAT;
    if (!sand_ed25519_verify2(signature, public_key,
                              SAND_STCI_SAPP_V2_DOMAIN, SAND_STCI_SAPP_V2_DOMAIN_SIZE,
                              image, signature_offset))
        return SAND_STCI_ERR_SIGNATURE;
    return SAND_STCI_OK;
}
