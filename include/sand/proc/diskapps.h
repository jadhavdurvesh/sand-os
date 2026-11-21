#ifndef SAND_PROC_DISKAPPS_H
#define SAND_PROC_DISKAPPS_H
#include <stdint.h>
/* Apps stored in MIMIR as "<name>.sapp" (written into the disk image by tools/mkmimir.py).
 * Looked up only after the built-in table, so a disk file can never shadow a built-in app. */
int diskapps_find(const char *name, const uint8_t **image, uint32_t *size);   /* image valid until the next call */
int diskapps_name_at(uint32_t index, char *name_out, uint32_t capacity);      /* name without ".sapp" */
/* Permission bits (sand/kernel/perms.h) declared in the SAPP metadata ("permissions"); 0 on any parse problem. */
uint32_t diskapps_manifest_permissions(const uint8_t *image, uint32_t size);
/* Launcher entries declared by disk apps ("launcher" object in their metadata, same rules as built-in manifests).
 * diskapps_launcher_scan() reads every .sapp once (caller must keep IRQs off or no other spawn running). */
struct sand_app_launcher_info;
int diskapps_manifest_launcher(const uint8_t *image, uint32_t size, const char *app, struct sand_app_launcher_info *out, uint32_t *order_out);
void diskapps_launcher_scan(void);
uint32_t diskapps_launcher_count(void);
int diskapps_launcher_at(uint32_t index, struct sand_app_launcher_info *out);
#endif
