#ifndef SAND_VEYRA_SUBSYSTEMS_H
#define SAND_VEYRA_SUBSYSTEMS_H
#include <stdint.h>
#include "sand/veyra/interface.h"
/*
 * Veyra subsystem descriptors for the interfaces SAND has implemented so far.
 *
 * Every descriptor starts with struct sand_veyra_interface_info (so consumers validate it with
 * sand_veyra_interface_valid) and is EXPERIMENTAL until its owner publishes a compatibility policy.
 * Applications discover a subsystem with the VEYRA_QUERY syscall instead of assuming it exists:
 * a missing subsystem (for example no network card) answers "not found" rather than failing later.
 * The descriptor says WHAT exists and its hard limits; the permission field says which manifest
 * capability the calls need. It never carries kernel pointers.
 */
#define SAND_VEYRA_IF_FILES   0x0101u  /* BEDROCK: whole-file read/write on SANDFS */
#define SAND_VEYRA_IF_IPC     0x0102u  /* TIDEWAY: named message channels */
#define SAND_VEYRA_IF_NOTIFY  0x0103u  /* HERALD: desktop notifications and background timers */
#define SAND_VEYRA_IF_NET     0x0104u  /* DRIFTNET: Ethernet, ARP, IPv4, ICMP echo, UDP datagrams */
#define SAND_VEYRA_IF_COUNT   4u

#define SAND_VEYRA_CAP_SEND   0x00000010u   /* subsystem-specific: can transmit */
#define SAND_VEYRA_CAP_RECEIVE 0x00000020u  /* subsystem-specific: can receive */

struct sand_veyra_subsystem_info {
    struct sand_veyra_interface_info base;  /* header (magic, version, size, flags) + interface_id + capabilities */
    uint32_t revision;                      /* subsystem revision, bumped on any incompatible change */
    uint32_t permission;                    /* SAND_PERM_* bit the calls need (0 = open to every app) */
    uint32_t limit[4];                      /* subsystem limits, meaning fixed per interface (see docs/VEYRA_ABI.md) */
};

/* Kernel-side translation from a Veyra subsystem status to the syscall layer's SAND_E_* result. */
int32_t sand_veyra_to_syscall(sand_veyra_status_t status);
/* Fills the descriptor for `interface_id`; SAND_VEYRA_E_NOT_FOUND for unknown ids or subsystems that are absent on this machine.
 * Interface id 0 returns the number of interfaces in limit[0] (the ENUMERATE capability). */
sand_veyra_status_t veyra_query(uint32_t interface_id, struct sand_veyra_subsystem_info *out);
#endif
