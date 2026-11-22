#ifndef SAND_SECURITY_OATH_H
#define SAND_SECURITY_OATH_H
#include <stdint.h>
/*
 * OATH: the signed boot profile that provisions the STCI launch gate.
 *
 * "boot.oath" is a text file stored in MIMIR and signed (Ed25519) with the platform key linked into the kernel (the same key that signs
 * factory.sigil). It carries the launch policy and the publisher trust roots:
 *     MIMIR-OATH 1\n
 *     policy require-signed | developer\n
 *     root <key id: 64 hex> <Ed25519 public key: 64 hex>\n     (at most 8)
 *     revoke <publisher key id: 64 hex>\n                      (at most 8)
 *     sig <128 hex>\n        signature over "MIMIR-OATH-V1\0" + every byte before this line
 * Loaded once at boot, after the boot self-tests and before user software runs:
 *   - no boot.oath: nothing changes (the built-in DEVELOPER profile with no trust roots, as before);
 *   - valid: the gate is configured with the profile's policy and roots;
 *   - present but unverifiable: fail closed (REQUIRE_SIGNED with no roots: only the kernel's built-in apps run).
 * Apps embedded in the kernel image are covered by the kernel's own integrity and are not subject to the gate; disk and installed apps are.
 */
#define SAND_OATH_ABSENT 0u
#define SAND_OATH_LOADED 1u
#define SAND_OATH_INVALID 2u
uint32_t sand_oath_load(void);
uint32_t sand_oath_state(void);
#endif
