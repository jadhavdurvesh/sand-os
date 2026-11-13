#ifndef SAND_PROC_APPS_H
#define SAND_PROC_APPS_H
#include <stdint.h>
/* Built-in application table: SAPP images linked into the kernel image by the
 * build (see Makefile). A future package manager will replace this with
 * images read from SANDFS. */
int apps_find(const char *name, const uint8_t **image, uint32_t *size);
int apps_name_at(uint32_t index, const char **name);
#endif
