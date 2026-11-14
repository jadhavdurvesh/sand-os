#include "sand/fs/sandfs.h"
#include "sand/storage/block.h"
#define SANDFS_MAGIC 0x53414E44u
#define SANDFS_VERSION 1u
#define SANDFS_DIRECTORY_LBA_OFFSET 1u
#define SANDFS_DATA_LBA_OFFSET 2u
#define SANDFS_ENTRY_COUNT 16u
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
/* Files are contiguous runs of whole sectors (v1 files are the one-sector special case), up to SANDFS_FILE_MAX bytes. */
static uint32_t sectors_for(uint32_t length) { return (length+511u)/512u; }
/* First-fit search for `count` free sectors (relative LBAs), ignoring entry `skip`. Returns 0 when none. */
static uint32_t find_run(uint32_t count,uint32_t skip) {
    uint32_t candidate=SANDFS_DATA_LBA_OFFSET+1u;
    for(;;) {
        if(candidate+count>mounted_count) return 0u;
        uint32_t bump=0u;
        for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) {
            if(i==skip || entries[i].name[0]=='\0') continue;
            uint32_t first=entries[i].data_lba-mounted_start, last=first+sectors_for(entries[i].length);   /* [first,last) */
            if(candidate<last && first<candidate+count && last>bump) bump=last;
        }
        if(bump==0u) return candidate;
        candidate=bump;
    }
}
int sandfs_write(const char *name,const void *data,uint32_t length) {
    if(mounted_device==0 || !valid_name(name) || data==0 || length==0u || length>SANDFS_FILE_MAX) return -1;
    uint32_t slot=SANDFS_ENTRY_COUNT, free_slot=SANDFS_ENTRY_COUNT;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { if(entries[i].name[0]=='\0') { if(free_slot==SANDFS_ENTRY_COUNT) free_slot=i; continue; } uint32_t j=0; while(name[j] && entries[i].name[j]==name[j]) ++j; if(name[j]=='\0' && entries[i].name[j]=='\0') { slot=i; break; } }
    if(slot==SANDFS_ENTRY_COUNT) slot=free_slot;
    if(slot==SANDFS_ENTRY_COUNT) return -1;
    uint32_t count=sectors_for(length), data_lba=find_run(count,slot);
    if(data_lba==0u) return -1;
    const uint8_t *src=(const uint8_t *)data;
    for(uint32_t sec=0;sec<count;++sec) {
        for(uint32_t i=0;i<512u;++i) { uint32_t off=sec*512u+i; sector_buffer[i]=off<length?src[off]:0u; }
        if(mounted_device->write(mounted_start+data_lba+sec,sector_buffer)!=0) return -1;
    }
    for(uint32_t i=0;i<SANDFS_NAME_MAX;++i) entries[slot].name[i]=0;
    for(uint32_t i=0;name[i] && i<SANDFS_NAME_MAX-1u;++i) entries[slot].name[i]=name[i];
    entries[slot].data_lba=mounted_start+data_lba; entries[slot].length=length;
    return sync_directory();
}
int sandfs_read(const char *name,void *data,uint32_t capacity,uint32_t *length_out) {
    if(mounted_device==0 || !valid_name(name) || data==0 || length_out==0) return -1;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) { uint32_t j=0; while(name[j] && entries[i].name[j]==name[j]) ++j; if(name[j]=='\0' && entries[i].name[j]=='\0' && entries[i].name[0]!='\0') {
        uint32_t length=entries[i].length, count=sectors_for(length);
        if(length>capacity || length>SANDFS_FILE_MAX || entries[i].data_lba<mounted_start || entries[i].data_lba>=mounted_start+mounted_count || count>mounted_start+mounted_count-entries[i].data_lba) return -1;
        uint8_t *dst=(uint8_t *)data;
        for(uint32_t sec=0;sec<count;++sec) {
            if(mounted_device->read(entries[i].data_lba+sec,sector_buffer)!=0) return -1;
            for(uint32_t k=0;k<512u && sec*512u+k<length;++k) dst[sec*512u+k]=sector_buffer[k];
        }
        *length_out=length; return 0;
    } }
    return -1;
}
int sandfs_list(uint32_t ordinal,char *name_out,uint32_t name_capacity,uint32_t *length_out) {
    if(mounted_device==0 || name_out==0 || name_capacity<SANDFS_NAME_MAX || length_out==0) return -1;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) {
        if(entries[i].name[0]=='\0') continue;
        if(ordinal--!=0u) continue;
        for(uint32_t k=0;k<SANDFS_NAME_MAX;++k) name_out[k]=entries[i].name[k];
        name_out[SANDFS_NAME_MAX-1u]='\0';
        *length_out=entries[i].length; return 0;
    }
    return -1;
}
int sandfs_remove(const char *name) {
    if(mounted_device==0 || !valid_name(name)) return -1;
    for(uint32_t i=0;i<SANDFS_ENTRY_COUNT;++i) {
        uint32_t j=0; while(name[j] && entries[i].name[j]==name[j]) ++j;
        if(name[j]=='\0' && entries[i].name[j]=='\0' && entries[i].name[0]!='\0') {
            for(uint32_t k=0;k<sizeof(entries[i]);++k) ((uint8_t *)&entries[i])[k]=0u;
            return sync_directory();
        }
    }
    return -1;
}
int sandfs_is_mounted(void) { return mounted_device!=0; }
