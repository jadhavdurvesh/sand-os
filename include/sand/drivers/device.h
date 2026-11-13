#ifndef SAND_DRIVERS_DEVICE_H
#define SAND_DRIVERS_DEVICE_H
#include <stdint.h>
/* SAND device registry: every driver announces the hardware it controls so
 * the system (and `devices` in the shell) can enumerate it. */
#define SAND_DEVICE_MAX 16u
enum sand_device_status { SAND_DEVICE_ABSENT = 0, SAND_DEVICE_READY = 1, SAND_DEVICE_FAILED = 2 };
struct sand_device { const char *name; const char *kind; const char *driver; enum sand_device_status status; };
int device_register(const char *name, const char *kind, const char *driver, enum sand_device_status status);
int device_set_status(const char *name, enum sand_device_status status);
uint32_t device_count(void);
const struct sand_device *device_get(uint32_t index);
const struct sand_device *device_find(const char *name);
#endif
