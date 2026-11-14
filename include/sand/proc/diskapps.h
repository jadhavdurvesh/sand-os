#ifndef SAND_PROC_DISKAPPS_H
#define SAND_PROC_DISKAPPS_H
#include <stdint.h>
/* Apps stored in SANDFS as "<name>.sapp" (written into the disk image by tools/mksandfs.py).
 * Looked up only after the built-in table, so a disk file can never shadow a built-in app. */
int diskapps_find(const char *name, const uint8_t **image, uint32_t *size);   /* image valid until the next call */
int diskapps_name_at(uint32_t index, char *name_out, uint32_t capacity);      /* name without ".sapp" */
/* Permission bits (sand/kernel/perms.h) declared in the SAPP metadata ("permissions"); 0 on any parse problem. */
uint32_t diskapps_manifest_permissions(const uint8_t *image, uint32_t size);
#endif
