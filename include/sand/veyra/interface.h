#ifndef SAND_VEYRA_INTERFACE_H
#define SAND_VEYRA_INTERFACE_H

#include <stdint.h>
#include "sand/veyra/abi.h"
#include "sand/veyra/status.h"

/*
 * Common prefix for published Veyra interface tables. Implementations should
 * append function pointers only after this prefix and publish the exact size.
 * Function-table layouts are not stable until their owning subsystem marks
 * them supported in the Veyra catalogue.
 */
struct sand_veyra_interface_info {
    struct sand_veyra_interface header;
    uint32_t interface_id;
    uint32_t capabilities;
};

#define SAND_VEYRA_CAP_READ       0x00000001u
#define SAND_VEYRA_CAP_WRITE      0x00000002u
#define SAND_VEYRA_CAP_CONTROL    0x00000004u
#define SAND_VEYRA_CAP_ENUMERATE  0x00000008u

#endif
