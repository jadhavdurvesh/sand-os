#ifndef SAND_STORAGE_BLOCK_H
#define SAND_STORAGE_BLOCK_H
#include <stdint.h>
#define SAND_BLOCK_SECTOR_SIZE 512u
struct sand_block_device { const char *name; uint32_t sector_count; int (*read)(uint32_t lba,uint8_t *buffer); int (*write)(uint32_t lba,const uint8_t *buffer); };
int block_register(const struct sand_block_device *device);
const struct sand_block_device *block_get(uint32_t index);
uint32_t block_count(void);
#endif
