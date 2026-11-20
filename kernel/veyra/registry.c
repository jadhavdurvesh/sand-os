#include "sand/veyra/subsystems.h"
#include "sand/kernel/perms.h"
#include "sand/ipc/tideway.h"
#include "sand/net/driftnet.h"
#include "sand/fs/sandfs.h"
#include "sand/abi.h"

int32_t sand_veyra_to_syscall(sand_veyra_status_t status) {
    switch(status) {
    case SAND_VEYRA_OK: return 0;
    case SAND_VEYRA_E_NOT_FOUND: return SAND_E_NODEV;
    case SAND_VEYRA_E_TIMEOUT: return SAND_E_TIMEOUT;
    case SAND_VEYRA_E_PERMISSION: return SAND_E_PERM;
    case SAND_VEYRA_E_BUSY: return SAND_E_FULL;
    default: return SAND_E_FAIL;
    }
}
static void fill(struct sand_veyra_subsystem_info *o, uint32_t id, uint32_t caps, uint32_t perm, uint32_t l0, uint32_t l1, uint32_t l2, uint32_t l3) {
    o->base.header.magic = SAND_VEYRA_MAGIC; o->base.header.abi_major = SAND_VEYRA_ABI_MAJOR; o->base.header.abi_minor = SAND_VEYRA_ABI_MINOR;
    o->base.header.struct_size = sizeof(*o); o->base.header.flags = SAND_VEYRA_IFACE_EXPERIMENTAL;
    o->base.interface_id = id; o->base.capabilities = caps; o->revision = 1u; o->permission = perm;
    o->limit[0] = l0; o->limit[1] = l1; o->limit[2] = l2; o->limit[3] = l3;
}
sand_veyra_status_t veyra_query(uint32_t id, struct sand_veyra_subsystem_info *o) {
    if(!o) return SAND_VEYRA_E_INVALID;
    switch(id) {
    case 0u: fill(o, 0u, SAND_VEYRA_CAP_ENUMERATE, 0u, SAND_VEYRA_IF_COUNT, 0u, 0u, 0u); return SAND_VEYRA_OK;
    /* limit: file max bytes, entry count, name length, 0 */
    case SAND_VEYRA_IF_FILES: fill(o, id, SAND_VEYRA_CAP_READ | SAND_VEYRA_CAP_WRITE | SAND_VEYRA_CAP_ENUMERATE, SAND_PERM_FS_READ | SAND_PERM_FS_WRITE, SANDFS_FILE_MAX, 15u, 31u, 0u); return SAND_VEYRA_OK;
    /* limit: channels, queue depth, message bytes, name length */
    case SAND_VEYRA_IF_IPC: fill(o, id, SAND_VEYRA_CAP_SEND | SAND_VEYRA_CAP_RECEIVE | SAND_VEYRA_CAP_CONTROL, SAND_PERM_IPC, TIDEWAY_CHANNELS, TIDEWAY_QUEUE, TIDEWAY_MSG_MAX, TIDEWAY_NAME_MAX - 1u); return SAND_VEYRA_OK;
    /* limit: pending timers per client service, text bytes, toast seconds, longest timer seconds. Reached through the IPC channels "herald.*". */
    case SAND_VEYRA_IF_NOTIFY: fill(o, id, SAND_VEYRA_CAP_SEND | SAND_VEYRA_CAP_CONTROL, SAND_PERM_IPC, 8u, 60u, 5u, 86400u); return SAND_VEYRA_OK;
    /* limit: datagram bytes, UDP sockets, concurrent pings, concurrent TCP connections; absent without a NIC. Revision 2 added DHCP and the TCP client. */
    case SAND_VEYRA_IF_NET: {
        struct driftnet_info ni;
        if(driftnet_info(&ni) != 0) return SAND_VEYRA_E_NOT_FOUND;
        fill(o, id, SAND_VEYRA_CAP_SEND | SAND_VEYRA_CAP_RECEIVE | SAND_VEYRA_CAP_READ, SAND_PERM_NET, DRIFTNET_UDP_MAX, 4u, 1u, 1u); o->revision = 2u; return SAND_VEYRA_OK; }
    /* limit: longest name (63), cache entries (4), cap on cached lifetime in seconds (300), concurrent lookups (1); needs the NIC */
    case SAND_VEYRA_IF_NAMES: {
        struct driftnet_info ni;
        if(driftnet_info(&ni) != 0) return SAND_VEYRA_E_NOT_FOUND;
        fill(o, id, SAND_VEYRA_CAP_READ, SAND_PERM_NET, 63u, 4u, 300u, 1u); return SAND_VEYRA_OK; }
    default: return SAND_VEYRA_E_NOT_FOUND;
    }
}
