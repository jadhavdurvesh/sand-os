#ifndef SAND_FS_SANDFS_H
#define SAND_FS_SANDFS_H
#include <stdint.h>
#define SANDFS_NAME_MAX 24u
int sandfs_format(uint32_t device_index,uint32_t start_lba,uint32_t sector_count);
int sandfs_mount(uint32_t device_index,uint32_t start_lba);
int sandfs_write(const char *name,const void *data,uint32_t length);
int sandfs_read(const char *name,void *data,uint32_t capacity,uint32_t *length_out);
#endif
