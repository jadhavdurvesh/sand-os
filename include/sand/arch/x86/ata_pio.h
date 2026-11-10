#ifndef SAND_ARCH_X86_ATA_PIO_H
#define SAND_ARCH_X86_ATA_PIO_H
#include <stdint.h>
int ata_pio_init(void);
int ata_pio_read(uint32_t lba,uint8_t *buffer);
int ata_pio_write(uint32_t lba,const uint8_t *buffer);
uint32_t ata_pio_sector_count(void);
#endif
