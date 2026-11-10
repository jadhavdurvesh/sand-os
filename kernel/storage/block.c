#include "sand/storage/block.h"
#define BLOCK_DEVICE_LIMIT 4u
static const struct sand_block_device *devices[BLOCK_DEVICE_LIMIT];
static uint32_t device_total;
int block_register(const struct sand_block_device *device) {
    if(device==0 || device->name==0 || device->sector_count==0u || device->read==0 || device->write==0 || device_total>=BLOCK_DEVICE_LIMIT) return -1;
    for(uint32_t i=0;i<device_total;++i) if(devices[i]==device) return -1;
    devices[device_total++]=device; return 0;
}
const struct sand_block_device *block_get(uint32_t index) { return index<device_total?devices[index]:0; }
uint32_t block_count(void) { return device_total; }
