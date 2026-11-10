# SAND Veyra interface catalogue

**SAND Veyra** is the native interface architecture of SAND OS: the family of SAND-defined ABI, subsystem, service and SDK contracts. The generic terms API, ABI, syscall and interface remain technically valid and should continue to be used where precise. Veyra is the architecture's identity, not a claim that all interfaces are complete.

This catalogue distinguishes implemented interfaces from prototypes and future contracts. Presence of a header does not mean an interface is stable or safe for general applications.

## Veyra layers

| Layer | Purpose | Current state |
|---|---|---|
| Kernel ABI | Privilege boundary and stable low-level system-call contract | ABI v1 proof of concept; not yet a general-purpose stable ABI |
| Subsystem interfaces | SAND-owned contracts for memory, devices, storage, display, input, graphics, audio and files | Several early internal interfaces; no complete public contract set |
| Service contracts | IPC, lifecycle and message contracts between isolated services | Not implemented |
| Native developer interfaces | SDK/runtime interfaces for native applications | Early SAPP SDK; incomplete runtime |

Veyra implementation rules:

- Version public contracts and define compatibility and error conventions.
- Validate user pointers, lengths, handles and message contents at every privilege boundary.
- Keep kernel-private data structures out of public ABI.
- Keep hardware protocol details in drivers behind SAND-owned subsystem contracts.
- Keep higher-level frameworks, including CONTRACT, outside the kernel and connect them through adapters.
- Do not mark a contract stable until it has specification, implementation, tests and compatibility policy.

## Existing SAND-owned interfaces

| Area | Current interface / location | What exists | Maturity and limits |
|---|---|---|---|
| User syscall ABI | `sdk/sapp/include/sand/app.h`, `kernel/arch/x86/syscall.c` | ABI version 1; exit, write and get-ticks syscall numbers; x86 `int 0x80` wrappers | Very early ABI. Pointer validation, full argument copying, version negotiation and a stable compatibility policy are not complete |
| Process management | `include/sand/kernel/process.h`, `kernel/proc/process.c` | Bounded process table and address-space lifecycle primitives | Prototype; no complete user-process scheduler, wait/exec/fork lifecycle or robust fault isolation |
| Kernel scheduling | `include/sand/kernel/scheduler.h`, `kernel/sched/` | Kernel threads, preemption and basic synchronization | Early uniprocessor implementation; not SMP-safe |
| Physical memory / paging | `include/sand/mm/`, `include/sand/arch/x86/paging.h` | Frame allocator, non-PAE paging and early heap | Bring-up implementation; bounded frame window and no heap free |
| Block storage | `include/sand/storage/block.h`, `kernel/storage/block.c` | SAND-owned sector read/write callback registration | Fixed early interface; no general device lifecycle, async I/O, queueing or partition manager |
| ATA PIO | `include/sand/arch/x86/ata_pio.h`, `kernel/arch/x86/ata_pio.c` | Primary-master ATA PIO backend | Hardware-specific initial driver; not a general bus or storage stack |
| Filesystem | `include/sand/fs/sandfs.h`, `kernel/fs/sandfs.c` | SANDFS v1 format, mount, read and write | One-sector-per-file prototype; not a mature hierarchical filesystem or POSIX VFS |
| Display | `include/sand/graphics/framebuffer.h`, `include/sand/arch/x86/vbe.h` | Framebuffer and drawing primitives; VBE setup | Single early display backend; no general display-device framework |
| Input | `include/sand/arch/x86/input.h`, `kernel/arch/x86/ps2.c` | Raw keyboard and pointer events via polling | Prototype. No normalized event queue, IRQ-driven delivery or desktop event loop |
| Window management | `include/sand/graphics/window.h`, `kernel/graphics/window.c` | Bounded window records, focus/z-order, basic pointer dragging and rendering | Prototype only; no compositor, app surfaces, input integration, close controls or full desktop |
| SAPP executable / SDK | `sdk/sapp/` | SAPP format specification, builder, example and parser tests | Early custom application format; runtime and process execution still limited |

## Important Veyra gaps before third-party/native applications

- A versioned, documented syscall ABI with safe user-memory access and error conventions.
- A kernel object and handle model, plus IPC/message passing.
- A coherent device model with driver registration, resource ownership, lifecycle and capabilities.
- A normalized input event API and event delivery to processes.
- A display-device API and surface/presentation contract.
- A real compositor contract, window-server boundary and desktop protocol.
- A VFS-like filesystem API, directory semantics, mount model and file handles.
- Network interfaces, packet stack, sockets and DNS/TLS userland support.
- Audio device/stream APIs and power-management interfaces.
- Service supervision, logging, configuration and security policy.

## DMJ technology integration assessment

Integration means implementing an adapter against a documented SAND interface. It does not mean copying every project's source into the kernel.

| Project | Potential integration | Required SAND capability | Recommendation |
|---|---|---|---|
| CALLGRAPH | Service-to-service typed RPC / remote operations | Network stack, UDP transport, IPC, process runtime, compatible cryptography and serialization | High-value future integration; keep it outside the kernel |
| OCTET | Binary serialization for messages and storage protocols | Stable byte buffers, IPC and network transport | Reuse as a protocol/library only after format and portability audit |
| DATAGRAM | UDP transport engine | Network interface, IP/UDP stack or a clearly defined lower transport adapter | Future networking integration; do not pretend sockets exist yet |
| DUSRM | Architecture responsibility and contract validation | Documented subsystem boundaries, ownership and conformance checks | Use as an architecture governance method; it is not a driver or runtime |
| EREBUS | Firewall/policy engine | Packet/flow hook, network stack, isolated service runtime and fail-safe enforcement | Future privileged service, not kernel source |
| ANANTA | Database engine for structured application/system data | Files, memory, process runtime and storage reliability | User-space service/library; never substitute for SANDFS/VFS |
| CONTRACT | API/service framework | User runtime, networking, process isolation and language runtime | Independent framework; a future Veyra adapter may expose native SAND facilities |
| NOCTIS | Native browser | Graphics, input, processes, networking, TLS and storage | Application, not OS core |
| MAINTAIN AI | Device management / industrial telemetry UI and service | Device enumeration, IPC, networking, graphics and storage | Application/service; device contracts should be designed generically |
| DMJScript and other language/toolchain projects | Compiler target, runtime and SDK bindings | Stable executable format, syscall ABI, linker and runtime contracts | Add target only after ABI stabilizes |
| Relay / CONTINUUM / DMJ LLM / Gönül | AI and engineering applications/services | Process runtime, memory, graphics, networking, storage and accelerator support | User-space workloads; not kernel dependencies |
| FireBot ecosystem | Engineering, simulation and supervisory tools | Device APIs, networking, graphics and robust process isolation | Host tooling only; safety-critical robot control remains on dedicated controllers |

## CALLGRAPH-specific boundary

CALLGRAPH should not be embedded in ring-0 kernel code. Its typed operation protocol and transport machinery belong in a privileged or ordinary user-space service once SAND has process isolation and networking. The intended layering is:

1. SAND network driver and packet stack.
2. SAND socket/datagram API.
3. CALLGRAPH transport adapter using its existing protocol implementation.
4. CALLGRAPH service endpoint and typed operation registry.
5. Native SAND applications and system services as clients.

Before porting, audit CALLGRAPH's language/runtime dependencies, cryptographic implementation, serialization compatibility, transport assumptions, memory ownership and error model. Preserve its independent repository and release lifecycle.

## Integration rules

- No external project is a kernel dependency by default.
- Do not claim a project is integrated until a working adapter, tests and documented compatibility target exist.
- Keep hardware drivers and safety-critical control isolated from general application frameworks.
- Prefer shared standards and stable interfaces over source-code duplication.
- Record every integration as proposed, in development, tested or supported.
