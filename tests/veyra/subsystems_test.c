#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "sand/abi.h"
#include "sand/veyra/subsystems.h"
#include "sand/net/driftnet.h"

/* The NIC is the one optional subsystem: the test toggles its presence. */
static int nic_present;
sand_veyra_status_t driftnet_info(struct driftnet_info *out) { (void)out; return nic_present ? SAND_VEYRA_OK : SAND_VEYRA_E_NOT_FOUND; }

int main(void)
{
    struct sand_veyra_subsystem_info d;
    static const uint32_t ids[] = { SAND_VEYRA_IF_FILES, SAND_VEYRA_IF_IPC, SAND_VEYRA_IF_NOTIFY, SAND_VEYRA_IF_NET };

    /* the kernel struct and the SDK mirror must stay byte-compatible */
    assert(sizeof(struct sand_veyra_subsystem_info) == sizeof(struct sand_veyra_subsystem_abi));
    assert(sizeof(struct sand_veyra_subsystem_info) == 48u);

    /* id 0 enumerates */
    assert(veyra_query(0u, &d) == SAND_VEYRA_OK);
    assert(d.limit[0] == SAND_VEYRA_IF_COUNT && (d.base.capabilities & SAND_VEYRA_CAP_ENUMERATE));

    nic_present = 1;
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        assert(veyra_query(ids[i], &d) == SAND_VEYRA_OK);
        assert(sand_veyra_interface_valid(&d.base.header, sizeof(d)));       /* magic, major version, size */
        assert(d.base.interface_id == ids[i]);
        assert(d.base.header.flags & SAND_VEYRA_IFACE_EXPERIMENTAL);          /* nothing is declared stable yet */
        assert(d.revision >= 1u && d.permission != 0u);
        for (unsigned j = 0; j < i; ++j) assert(ids[j] != ids[i]);            /* ids are unique */
    }
    /* documented limits */
    veyra_query(SAND_VEYRA_IF_FILES, &d); assert(d.limit[0] == 65536u);
    veyra_query(SAND_VEYRA_IF_IPC, &d);   assert(d.limit[0] == 8u && d.limit[1] == 8u && d.limit[2] == 112u);
    veyra_query(SAND_VEYRA_IF_NET, &d);   assert(d.limit[0] == 512u && d.limit[3] == 1u && d.revision == 2u && (d.base.capabilities & SAND_VEYRA_CAP_SEND));

    /* absent hardware is reported, unknown ids and bad arguments are rejected */
    nic_present = 0;
    assert(veyra_query(SAND_VEYRA_IF_NET, &d) == SAND_VEYRA_E_NOT_FOUND);
    assert(veyra_query(SAND_VEYRA_IF_FILES, &d) == SAND_VEYRA_OK);
    assert(veyra_query(0x7777u, &d) == SAND_VEYRA_E_NOT_FOUND);
    assert(veyra_query(SAND_VEYRA_IF_IPC, NULL) == SAND_VEYRA_E_INVALID);

    /* Veyra status -> syscall result */
    assert(sand_veyra_to_syscall(SAND_VEYRA_OK) == 0);
    assert(sand_veyra_to_syscall(SAND_VEYRA_E_TIMEOUT) == SAND_E_TIMEOUT);
    assert(sand_veyra_to_syscall(SAND_VEYRA_E_NOT_FOUND) == SAND_E_NODEV);
    assert(sand_veyra_to_syscall(SAND_VEYRA_E_PERMISSION) == SAND_E_PERM);
    assert(sand_veyra_to_syscall(SAND_VEYRA_E_BUSY) == SAND_E_FULL);
    assert(sand_veyra_to_syscall(SAND_VEYRA_E_IO) == SAND_E_FAIL);
    return 0;
}
