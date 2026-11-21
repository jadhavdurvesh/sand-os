#ifndef SAND_PROC_APPINSTALL_H
#define SAND_PROC_APPINSTALL_H
#include <stdint.h>
/* Stage an app image in a kernel buffer (owned by one process at a time) and store it in MIMIR as <name>.sapp.
 * Both return SAND_E_* codes (>= 0 on success). The caller must hold SAND_PERM_INSTALL (checked by the dispatcher).
 * Commit refuses: bad names, names of built-in apps, non-SAPP data, and anything the launch gate would deny
 * (v2 chain failures; unsigned images under REQUIRE_SIGNED). The gate runs again at every launch. */
int32_t appinstall_put(uint32_t pid, uint32_t offset, const void *data, uint32_t length);
int32_t appinstall_commit(uint32_t pid, const char *name);
#endif
