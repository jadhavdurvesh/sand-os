#include "sand/fs/mimir.h"
/* The Ed25519 public key that signs factory.sigil. The build generates a throw-away developer key pair (build/sigil/) and links the
 * public half in here with objcopy; a release image would embed a provisioned key instead. */
extern const uint8_t _binary_sigil_pub_bin_start[32];
const uint8_t *mimir_sigil_pubkey(void) { return _binary_sigil_pub_bin_start; }
