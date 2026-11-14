#ifndef SAND_SECURITY_LAUNCH_GATE_H
#define SAND_SECURITY_LAUNCH_GATE_H
#include <stdint.h>
#include "sand/security/stci.h"

/*
 * Application launch gate. process_create_sapp() calls sand_launch_gate() before it parses,
 * maps or runs anything.
 *
 *  - SAPP v2: always verified (signature block, publisher certificate, provisioned root,
 *    validity window, revocation). Any failure denies the launch.
 *  - SAPP v1 (unsigned): denied under SAND_STCI_POLICY_REQUIRE_SIGNED; otherwise allowed as a
 *    visible, audited DEVELOPER launch.
 *
 * The default policy is DEVELOPER and the built-in trust store has NO roots, so out of the box
 * the kernel still runs unsigned development apps and trusts no publisher. A production profile
 * must provision roots from the boot configuration and set REQUIRE_SIGNED; the policy can be
 * tightened at runtime only by kernel code (there is no syscall for it).
 */
int sand_launch_gate(const char *name, const void *image, uint32_t size);   /* 0 = allow, <0 = deny */

/* Kernel-internal: replace the active policy/trust store (NULL restores the built-in defaults).
 * The pointed-to objects must outlive their use. */
void sand_launch_gate_configure(const struct sand_stci_policy *policy, const struct sand_stci_trust_store *store);
/* The stored policy flags (for self-tests and status). */
uint32_t sand_launch_gate_flags(void);
/* Audit record: written to the debug console as "SAND_AUDIT:<text>". */
void sand_audit(const char *text);

#endif
