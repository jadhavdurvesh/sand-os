#ifndef SAND_STORAGE_BIFROST_H
#define SAND_STORAGE_BIFROST_H
#include <stdint.h>
/* BIFROST: native AHCI (SATA) driver. Finds the controller on PCI bus 0 (class 01:06, prog-if 01), maps its
 * registers, brings up each port that has a SATA disk and registers it as block device "sata0".."sata3".
 * Polled, one command slot, 48-bit LBA, 512-byte DMA transfers through a kernel bounce buffer. */
#define SAND_BIFROST_MAX_PORTS 4u
int bifrost_init(void);                      /* number of disks registered; 0 if no controller or no disk */
/* Non-destructive check of disk `index`: reads the last sector, writes a pattern there, reads it back,
 * verifies, then restores the original. 0 on success. */
int bifrost_self_test(uint32_t index);
int bifrost_seed_present(uint32_t index);   /* 1 when sector 0 starts with "BIFROST-SEED" (host-seeded test disk) */
#endif
