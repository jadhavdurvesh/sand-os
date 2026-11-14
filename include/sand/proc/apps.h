#ifndef SAND_PROC_APPS_H
#define SAND_PROC_APPS_H
#include <stdint.h>
/* Built-in application table: SAPP images linked into the kernel image by the
 * build (see Makefile). A future package manager will replace this with
 * images read from SANDFS. */
int apps_find(const char *name, const uint8_t **image, uint32_t *size);
int apps_name_at(uint32_t index, const char **name);

/* Launcher catalogue, generated from apps/<name>/manifest.json ("launcher" object) by
 * tools/gen_app_table.py. Only apps that declare one are listed, ordered by launcher.order. */
struct sand_app_launcher_info {
    char label[16], app[17], subtitle[24];
    uint32_t accent, accent2;
    char glyph;
};
uint32_t apps_launcher_count(void);
int apps_launcher_at(uint32_t index, struct sand_app_launcher_info *out);
#endif
