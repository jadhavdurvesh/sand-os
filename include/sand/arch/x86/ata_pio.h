#ifndef SAND_ARCH_X86_ATA_PIO_H
#define SAND_ARCH_X86_ATA_PIO_H
#include <stdint.h>
/* Legacy IDE/PATA PIO driver for all four classic positions:
 * 0 = primary master, 1 = primary slave, 2 = secondary master, 3 = secondary slave.
 * Each responding ATA disk is registered as a block device ("ata0".."ata3") in
 * discovery order and added to the device registry. */
#define SAND_ATA_MAX_DRIVES 4u
int ata_pio_init(void);                      /* probes every position; 0 if at least one disk */
uint32_t ata_pio_drive_count(void);
int ata_pio_drive_position(uint32_t drive_index, uint32_t *position_out);
int ata_pio_read(uint32_t lba,uint8_t *buffer);   /* legacy: first drive */
int ata_pio_write(uint32_t lba,const uint8_t *buffer);
uint32_t ata_pio_sector_count(void);
#endif
