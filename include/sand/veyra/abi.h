#ifndef SAND_VEYRA_ABI_H
#define SAND_VEYRA_ABI_H

#include <stdint.h>

/*
 * SAND Veyra: native interface architecture.
 *
 * These constants define the format of Veyra interface descriptors. They do
 * not imply that a general syscall ABI or service IPC is implemented yet.
 */
#define SAND_VEYRA_MAGIC             0x56594153u /* "SAYV" in little-endian */
#define SAND_VEYRA_ABI_MAJOR         1u
#define SAND_VEYRA_ABI_MINOR         0u
#define SAND_VEYRA_ABI_VERSION       ((SAND_VEYRA_ABI_MAJOR << 16) | SAND_VEYRA_ABI_MINOR)
#define SAND_VEYRA_DESCRIPTOR_MIN   16u

/* Interface descriptor flags. Unknown bits must be ignored by consumers. */
#define SAND_VEYRA_IFACE_OPTIONAL     0x00000001u
#define SAND_VEYRA_IFACE_EXPERIMENTAL 0x00000002u

struct sand_veyra_interface {
    uint32_t magic;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t struct_size;
    uint32_t flags;
};

static inline int sand_veyra_interface_valid(
    const struct sand_veyra_interface *interface_desc,
    uint32_t minimum_size)
{
    if (interface_desc == (const struct sand_veyra_interface *)0)
        return 0;
    if (interface_desc->magic != SAND_VEYRA_MAGIC)
        return 0;
    if (interface_desc->abi_major != SAND_VEYRA_ABI_MAJOR)
        return 0;
    if (interface_desc->struct_size < SAND_VEYRA_DESCRIPTOR_MIN ||
        interface_desc->struct_size < minimum_size)
        return 0;
    return 1;
}

#endif
