# SAND Veyra ABI foundation

Status: **foundation specification, version 1.0**. This is not yet a general-purpose system-call ABI.

## Purpose

Veyra is the native interface architecture of SAND OS. It defines conventions for the low-level kernel ABI, subsystem interfaces, service contracts and native developer interfaces. The ordinary engineering terms API, ABI, syscall and interface remain valid.

## Initial descriptor contract

The public headers are:

- `include/sand/veyra/abi.h` — ABI identity, version and common interface descriptor.
- `include/sand/veyra/status.h` — common signed status values.
- `include/sand/veyra/interface.h` — common interface metadata and capability bits.

Every published interface begins with `struct sand_veyra_interface`:

| Field | Type | Meaning |
|---|---|---|
| `magic` | `uint32_t` | Must equal `SAND_VEYRA_MAGIC` |
| `abi_major` | `uint16_t` | Must match the supported major version |
| `abi_minor` | `uint16_t` | Minor version for additive compatible evolution |
| `struct_size` | `uint32_t` | Actual descriptor size in bytes |
| `flags` | `uint32_t` | Optional/experimental interface flags |

Consumers must reject an unknown major version, validate the minimum structure size before reading optional fields, and ignore unknown flag bits. A major-version change may break compatibility; a minor-version change must be additive.

## Status convention

Veyra subsystem contracts return `sand_veyra_status_t`. Zero means success; negative values describe failure. Subsystems may define additional errors in their own documented ranges, but must not silently reuse a core status with a different meaning.

## Security and ABI rules

1. Never expose kernel pointers, compiler-dependent bitfields or private kernel structures as public ABI.
2. Use fixed-width integer types and explicit structure sizes.
3. Validate every user pointer, length, handle and offset before dereferencing or copying.
4. Define ownership and lifetime for every buffer and handle.
5. Keep hardware-specific registers and protocol details in drivers.
6. Do not publish a function table as stable until its owner supplies implementation, tests and compatibility guarantees.
7. Keep higher-level frameworks such as CONTRACT outside ring 0; integrate them through documented adapters.

## Current implementation boundary

The existing SAPP SDK header contains early syscall declarations, while the current kernel syscall dispatcher is still a ring-3 proof-of-concept test gate. The declared exit/write/tick operations are **not yet implemented as a complete usable syscall ABI**. Do not build general applications against them yet.

The descriptor and status headers are a reusable contract foundation. They do not add syscalls, IPC, device enumeration or service supervision by themselves. Those facilities will be introduced as independently tested Veyra subsystem contracts.

## Implemented subsystem descriptors (EXPERIMENTAL)

Four SAND-built subsystems now publish a `struct sand_veyra_subsystem_info` (`include/sand/veyra/subsystems.h`): a `sand_veyra_interface_info` prefix (validated with `sand_veyra_interface_valid`), a subsystem `revision`, the `SAND_PERM_*` bit its calls need, and four documented limits. Apps ask the kernel with syscall `VEYRA_QUERY` (36, open to every app) instead of assuming a subsystem exists; an absent one (no NIC) answers `SAND_E_NODEV`, id `0` returns the interface count. All carry `SAND_VEYRA_IFACE_EXPERIMENTAL`: no function table or layout is declared stable.

| Id | Subsystem | Name | Permission | limit[0..3] |
|---|---|---|---|---|
| `0x0101` | FILES | BEDROCK: whole-file read/write on SANDFS | `fs.read`+`fs.write` | max file bytes (65536), entries (15), name length (31), 0 |
| `0x0102` | IPC | TIDEWAY: named channels | `ipc.channel` | channels (8), queue depth (8), message bytes (112), name length (15) |
| `0x0103` | NOTIFY | HERALD: toasts and background timers over the `herald.*` channels | `ipc.channel` | pending timers (8), text bytes (60), toast seconds (5), longest timer seconds (86400) |
| `0x0104` | NET | DRIFTNET: Ethernet, ARP, IPv4, ICMP echo, UDP | `net.access` | datagram bytes (512), UDP sockets (4), concurrent pings (1), concurrent TCP connections (1); revision 2 |

Status convention. Inside the kernel the subsystems speak Veyra statuses (`driftnet_*` return `sand_veyra_status_t`; its own pending marker is `DRIFTNET_PING_PENDING` = -100, a documented subsystem value). The syscall layer keeps its established `SAND_E_*` results, and `sand_veyra_to_syscall` is the single translation point (OK to 0, NOT_FOUND to NODEV, TIMEOUT to TIMEOUT, PERMISSION to PERM, BUSY to FULL, everything else to FAIL). The two numbering schemes are different on purpose and must not be mixed: for example `SAND_E_PERM` is -5 while `SAND_VEYRA_E_UNSUPPORTED` is -5 and `SAND_VEYRA_E_PERMISSION` is -6.

Security path. Permission checks for these calls run in the same dispatcher gate as every other call (denied: `SAND_E_PERM` plus a `PERMISSION_DENIED` audit line), user buffers are validated and copied before use, descriptors carry no kernel pointers, TIDEWAY stamps the sender pid in the kernel, and DRIFTNET logs every egress (`SAND_AUDIT:NET_UDP_SEND <ip>:<port> len=<n>`, `NET_PING <ip>`, `NET_TCP_CONNECT <ip>:<port>`). Under the STCI launch gate, manifest permissions are only as trustworthy as the profile: the DEVELOPER profile accepts unsigned apps that declare their own permissions; a signed `REQUIRE_SIGNED` boot profile (not provisioned yet) is what would make a manifest's `net.access` an authorised grant. The contract test `tests/veyra/subsystems_test.c` (host, ASan/UBSan) checks descriptor validity, unique ids, limits, absent-NIC behaviour, rejection of unknown ids and null output, and the status translation.

## Compatibility and validation test coverage

The host-side contract test at `tests/veyra/abi_test.c` is compiled and executed
by the native boot CI. It checks descriptor layout, valid and null descriptors,
magic and major-version rejection, minimum-size handling, unknown flag tolerance,
and the stable status convention.

The inline descriptor helper is intended for trusted, already-readable kernel
memory. It cannot make an arbitrary user pointer safe to dereference. Kernel
entry points must first validate user address ranges and copy descriptor bytes
into kernel-owned memory before calling the helper. This distinction is
mandatory for the future syscall and service boundary implementation.

## Current milestone boundary

M7 establishes the shared Veyra contract vocabulary and a tested descriptor
format. It does not yet publish stable subsystem function tables, a general
syscall dispatcher, capability enforcement, or IPC. Those are separate
milestone deliverables and must not be inferred from the presence of these
headers.
