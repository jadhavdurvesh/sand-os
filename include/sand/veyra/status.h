#ifndef SAND_VEYRA_STATUS_H
#define SAND_VEYRA_STATUS_H

#include <stdint.h>

/* Stable status values for future Veyra subsystem and service contracts. */
typedef int32_t sand_veyra_status_t;

enum sand_veyra_status {
    SAND_VEYRA_OK = 0,
    SAND_VEYRA_E_INVALID = -1,
    SAND_VEYRA_E_NOT_FOUND = -2,
    SAND_VEYRA_E_NO_MEMORY = -3,
    SAND_VEYRA_E_BUSY = -4,
    SAND_VEYRA_E_UNSUPPORTED = -5,
    SAND_VEYRA_E_PERMISSION = -6,
    SAND_VEYRA_E_IO = -7,
    SAND_VEYRA_E_VERSION = -8,
    SAND_VEYRA_E_BUFFER_TOO_SMALL = -9,
    SAND_VEYRA_E_TIMEOUT = -10
};

#endif
