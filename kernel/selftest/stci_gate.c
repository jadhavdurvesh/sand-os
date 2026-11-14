#include "sand/security/launch_gate.h"
#include "sand/security/stci.h"
#include "sand/kernel/process.h"
#include "sand/proc/apps.h"
#include "sand/mm/heap.h"

/* Boot self-test of the launch gate against a signed fixture generated at build time
 * (tools/stci_fixtures.py: throw-away test root + publisher; never trusted by the default store). */
extern const uint8_t _binary_signed_sapp_start[], _binary_signed_sapp_end[];
extern const uint8_t _binary_stci_fixture_bin_start[];

static void copy(uint8_t *d, const uint8_t *s, uint32_t n) { for (uint32_t i = 0; i < n; ++i) d[i] = s[i]; }

int sand_stci_gate_selftest(void) {
    const uint8_t *signed_img = _binary_signed_sapp_start; uint32_t size = (uint32_t)(_binary_signed_sapp_end - _binary_signed_sapp_start);
    struct sand_stci_root root; copy(root.key_id, _binary_stci_fixture_bin_start, 32); copy(root.public_key, _binary_stci_fixture_bin_start + 32, 32); root.flags = 0u;
    uint8_t publisher[SAND_STCI_SHA256_SIZE]; copy(publisher, _binary_stci_fixture_bin_start + 64, 32);
    const uint8_t (*revoked)[SAND_STCI_SHA256_SIZE] = (const uint8_t (*)[SAND_STCI_SHA256_SIZE])publisher;
    struct sand_stci_trust_store store = { SAND_STCI_ABI_MAJOR, sizeof(store), 1u, 1u, &root, 0, 0u };
    struct sand_stci_trust_store empty = { SAND_STCI_ABI_MAJOR, sizeof(empty), 0u, 1u, 0, 0, 0u };
    struct sand_stci_trust_store revoking = { SAND_STCI_ABI_MAJOR, sizeof(revoking), 1u, 1u, &root, revoked, 1u };
    struct sand_stci_policy strict = { SAND_STCI_ABI_MAJOR, sizeof(strict), SAND_STCI_POLICY_REQUIRE_SIGNED, 0u, 0 };
    struct sand_stci_policy dev = { SAND_STCI_ABI_MAJOR, sizeof(dev), SAND_STCI_POLICY_DEVELOPER_MODE, 0u, 0 };
    const uint8_t *unsigned_img = 0; uint32_t unsigned_size = 0;
    if (apps_find("hello", &unsigned_img, &unsigned_size) != 0) return 1;
    uint8_t *tampered = (uint8_t *)kmalloc(size);
    if (!tampered) return 2;
    int rc = 0;

    sand_launch_gate_configure(&strict, &store);
    if (sand_launch_gate("signed", signed_img, size) != 0) rc = 10;                                   /* valid signature, trusted root */
    copy(tampered, signed_img, size); tampered[size / 2] ^= 1u;                                       /* flip one payload/header/block bit */
    if (rc == 0 && sand_launch_gate("tampered", tampered, size) == 0) rc = 11;
    if (rc == 0 && sand_launch_gate("unsigned", unsigned_img, unsigned_size) == 0) rc = 12;           /* unsigned v1 under REQUIRE_SIGNED */
    sand_launch_gate_configure(&strict, &empty);
    if (rc == 0 && sand_launch_gate("no-root", signed_img, size) == 0) rc = 13;                       /* untrusted root */
    sand_launch_gate_configure(&strict, &revoking);
    if (rc == 0 && sand_launch_gate("revoked", signed_img, size) == 0) rc = 14;                       /* revoked publisher */
    struct sand_stci_policy early = strict; early.now_utc = "2001-01-01T00:00:00";
    sand_launch_gate_configure(&early, &store);
    if (rc == 0 && sand_launch_gate("early-clock", signed_img, size) == 0) rc = 15;                   /* outside validity window */
    sand_launch_gate_configure(&dev, &store);
    if (rc == 0 && sand_launch_gate("dev-unsigned", unsigned_img, unsigned_size) != 0) rc = 16;       /* developer mode: audited allow */
    /* the loader itself accepts the verified v2 image (and refuses the tampered one) */
    sand_launch_gate_configure(&strict, &store);
    sand_pid_t pid = 0u;
    if (rc == 0) { if (process_create_sapp("signed", signed_img, size, &pid) != 0) rc = 17; else (void)process_destroy(pid); }
    if (rc == 0 && process_create_sapp("tampered", tampered, size, &pid) == 0) { (void)process_destroy(pid); rc = 18; }
    sand_launch_gate_configure(0, 0);                                                                  /* back to defaults */
    return rc;
}
