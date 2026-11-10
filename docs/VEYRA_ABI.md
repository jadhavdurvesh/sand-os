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
