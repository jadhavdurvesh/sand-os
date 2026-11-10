#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "sand/veyra/abi.h"
#include "sand/veyra/interface.h"

int main(void)
{
    struct sand_veyra_interface descriptor = {
        .magic = SAND_VEYRA_MAGIC,
        .abi_major = SAND_VEYRA_ABI_MAJOR,
        .abi_minor = SAND_VEYRA_ABI_MINOR,
        .struct_size = sizeof(struct sand_veyra_interface),
        .flags = 0
    };

    assert(sizeof(struct sand_veyra_interface) == SAND_VEYRA_DESCRIPTOR_MIN);
    assert(sand_veyra_interface_valid(&descriptor, SAND_VEYRA_DESCRIPTOR_MIN));
    assert(sand_veyra_interface_valid(&descriptor, 8));
    assert(!sand_veyra_interface_valid(NULL, SAND_VEYRA_DESCRIPTOR_MIN));

    descriptor.magic ^= 1u;
    assert(!sand_veyra_interface_valid(&descriptor, SAND_VEYRA_DESCRIPTOR_MIN));
    descriptor.magic = SAND_VEYRA_MAGIC;

    descriptor.abi_major++;
    assert(!sand_veyra_interface_valid(&descriptor, SAND_VEYRA_DESCRIPTOR_MIN));
    descriptor.abi_major = SAND_VEYRA_ABI_MAJOR;

    descriptor.struct_size = SAND_VEYRA_DESCRIPTOR_MIN - 1u;
    assert(!sand_veyra_interface_valid(&descriptor, SAND_VEYRA_DESCRIPTOR_MIN));
    descriptor.struct_size = sizeof(struct sand_veyra_interface);

    assert(!sand_veyra_interface_valid(&descriptor, sizeof(struct sand_veyra_interface) + 1u));

    descriptor.flags = 0x80000000u;
    assert(sand_veyra_interface_valid(&descriptor, SAND_VEYRA_DESCRIPTOR_MIN));

    {
        struct sand_veyra_interface_info info = {
            .header = descriptor,
            .interface_id = 0x1001u,
            .capabilities = SAND_VEYRA_CAP_READ | SAND_VEYRA_CAP_ENUMERATE
        };
        assert(info.header.magic == SAND_VEYRA_MAGIC);
        assert((info.capabilities & SAND_VEYRA_CAP_READ) != 0);
    }

    return 0;
}
