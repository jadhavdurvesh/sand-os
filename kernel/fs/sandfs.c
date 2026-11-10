#include "sand/fs/sandfs.h"
#include "sand/storage/block.h"
#define SANDFS_MAGIC 0x53414E44u
#define SANDFS_VERSION 1u
#define SANDFS_DIRECTORY_LBA_OFFSET 1u
#define SANDFS_DATA_LBA_OFFSET 2u
#define SANDFS_ENTRY_COUNT 15u
struct sandfs_super { uint32_t magic; uint32_t version; uint32_t sector_count; uint32_t next_data; uint32_t directory_lba; uint32_t data_start; uint8_t reserved[488]; };
struct sandfs_entry { char name[SANDFS_NAME_MAX]; uint32_t data_lba; uint32_t length; };
static const struct sand_block_device *mounted_device;
static uint32_t mounted_start,mounted_count;
static uint8_t sector_buffer[512];
static struct sandfs_entry entries[SANDFS_ENTRY_COUNT];
static int valid_name(const char *name) { if(name==0 || name[0]=='\0') return 0; uint32_t n=0; while(name[n] && n<SANDFS_NAME_MAX) ++n; return n>0u && n<SANDFS_NAME_MAX; }
static int sync_directory(void) {
    for(uint32_t i=0;i<512u;++i) sector_buffer[i]=0u;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { uint32_t off=i*sizeof(struct sandfs_entry); const uint8_t *src=(const uint8_t *)&entries[i]; for(uint32_t j=0;j<sizeof(struct sandfs_entry);++j) sector_buffer[off+j]=src[j]; }
    return mounted_device->write(mounted_start+SANDFS_DIRECTORY_LBA_OFFSET,sector_buffer);
}
int sandfs_format(uint32_t device_index,uint32_t start_lba,uint32_t sector_count) {
    const struct sand_block_device *dev=block_get(device_index);
    if(dev==0 || sector_count<32u || start_lba>=dev->sector_count || sector_count>dev->sector_count-start_lba) return -1;
    struct sandfs_super super={0}; super.magic=SANDFS_MAGIC; super.version=SANDFS_VERSION; super.sector_count=sector_count; super.next_data=SANDFS_DATA_LBA_OFFSET+1u; super.directory_lba=SANDFS_DIRECTORY_LBA_OFFSET; super.data_start=SANDFS_DATA_LBA_OFFSET+1u;
    const uint8_t *src=(const uint8_t *)&super; for(uint32_t i=0;i<512u;++i) sector_buffer[i]=src[i];
    if(dev->write(start_lba,sector_buffer)!=0) return -1;
    for(uint32_t i=0;i<512u;++i) sector_buffer[i]=0u;
    return dev->write(start_lba+SANDFS_DIRECTORY_LBA_OFFSET,sector_buffer);
}
int sandfs_mount(uint32_t device_index,uint32_t start_lba) {
    const struct sand_block_device *dev=block_get(device_index); if(dev==0 || start_lba>=dev->sector_count) return -1;
    if(dev->read(start_lba,sector_buffer)!=0) return -1;
    struct sandfs_super super; uint8_t *dst=(uint8_t *)&super; for(uint32_t i=0;i<512u;++i) dst[i]=sector_buffer[i];
    if(super.magic!=SANDFS_MAGIC || super.version!=SANDFS_VERSION || super.sector_count<32u || super.sector_count>dev->sector_count-start_lba || super.directory_lba!=SANDFS_DIRECTORY_LBA_OFFSET || super.data_start<=super.directory_lba || super.data_start>=super.sector_count) return -1;
    if(dev->read(start_lba+super.directory_lba,sector_buffer)!=0) return -1;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { uint32_t off=i*sizeof(struct sandfs_entry); uint8_t *entry=(uint8_t *)&entries[i]; for(uint32_t j=0;j<sizeof(struct sandfs_entry);++j) entry[j]=sector_buffer[off+j]; }
    mounted_device=dev; mounted_start=start_lba; mounted_count=super.sector_count; return 0;
}
int sandfs_write(const char *name,const void *data,uint32_t length) {
    if(mounted_device==0 || !valid_name(name) || data==0 || length==0u || length>512u) return -1;
    uint32_t slot=SANDFS_ENTRY_COUNT;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { if(entries[i].name[0]=='\0' && slot==SANDFS_ENTRY_COUNT) slot=i; uint32_t j=0; while(name[j] && entries[i].name[j]==name[j]) ++j; if(name[j]=='\0' && entries[i].name[j]=='\0') { slot=i; break; } }
    if(slot==SANDFS_ENTRY_COUNT) return -1;
    if(mounted_count<=SANDFS_DATA_LBA_OFFSET+1u) return -1;
    uint32_t data_lba=SANDFS_DATA_LBA_OFFSET+1u;
    /* One-sector file store: locate the first unused data sector. */
    for(uint32_t candidate=data_lba;candidate<mounted_count;++candidate) { int used=0; for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) if(i!=slot && entries[i].name[0]!='\0' && entries[i].data_lba==mounted_start+candidate) used=1; if(!used) { data_lba=candidate; break; } }
    for(uint32_t i=0;i<512u;++i) sector_buffer[i]=0u;
    const uint8_t *src=(const uint8_t *)data; for(uint32_t i=0;i<length;++i) sector_buffer[i]=src[i];
    if(mounted_device->write(mounted_start+data_lba,sector_buffer)!=0) return -1;
    for(uint32_t i=0;i<SANDFS_NAME_MAX;++i) entries[slot].name[i]=0;
    for(uint32_t i=0;name[i] && i<SANDFS_NAME_MAX-1u;++i) entries[slot].name[i]=name[i];
    entries[slot].data_lba=mounted_start+data_lba; entries[slot].length=length;
    return sync_directory();
}
int sandfs_read(const char *name,void *data,uint32_t capacity,uint32_t *length_out) {
    if(mounted_device==0 || !valid_name(name) || data==0 || length_out==0) return -1;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { uint32_t j=0; while(name[j] && entries[i].name[j]==name[j]) ++j; if(name[j]=='\0' && entries[i].name[j]=='\0' && entries[i].name[0]!='\0') {
        if(entries[i].length>capacity || entries[i].length>512u || entries[i].data_lba<mounted_start || entries[i].data_lba>=mounted_start+mounted_count) return -1;
        if(mounted_device->read(entries[i].data_lba,sector_buffer)!=0) return -1;
        uint8_t *dst=(uint8_t *)data; for(uint32_t k=0;k<entries[i].length;++k) dst[k]=sector_buffer[k]; *length_out=entries[i].length; return 0;
    } }
    return -1;
}
