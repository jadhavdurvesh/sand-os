# SAND OS native naming standard

Status: canonical vocabulary for the SAND platform. Names identify subsystem ownership; they do not imply that every subsystem is implemented.

## Naming principles

1. SAND owns its architecture, interfaces and implementations. Familiar engineering words such as driver, process, syscall, filesystem, window, device and API remain valid in source code and technical documentation.
2. A branded name is the subsystem identity, not a replacement for precise API terminology.
3. Public names must be stable, pronounceable, searchable and documented before they become ABI commitments.
4. Hardware-facing code implements published hardware standards (PCI, PS/2, ATA, VBE, USB, etc.). SAND-specific names never imply invented hardware protocols.
5. Keep kernel mechanisms small. Prefer user-mode services for policy, UI and complex optional functionality when the process and IPC foundations support them.
6. Do not rename C symbols or directories solely for branding. Internal names should remain descriptive and grep-friendly.

## SAND Veyra

**SAND Veyra** is the official identity for SAND OS's native interface architecture: the coherent family of SAND-defined contracts through which kernel facilities, drivers, system services and native software communicate.

Veyra is an architectural umbrella, not a claim that every interface is already implemented, nor a synonym for the generic word API.

Veyra's intended layers are:

- **Kernel ABI:** stable low-level boundary for privilege transitions and system calls.
- **Subsystem interfaces:** typed contracts for memory, devices, storage, display, input, graphics, audio and other OS facilities.
- **Service contracts:** message and lifecycle interfaces between isolated system services.
- **Native developer interfaces:** documented higher-level interfaces exposed by the SAND SDK and runtime.

Rules:

- Keep the low-level ABI versioned and backward-compatible once published.
- Keep hardware protocol details in drivers; Veyra describes SAND's interface contracts above them.
- Validate all user-provided pointers, lengths, handles and message fields at privilege boundaries.
- Do not expose kernel pointers or internal structures as public ABI.
- Do not make the kernel depend on higher-level frameworks such as CONTRACT.
- CONTRACT remains an independent Rust API/service framework that may later target Veyra through an adapter.
- Use conventional terms such as API, syscall, ABI and interface wherever they are technically precise.

## Canonical subsystem vocabulary

| SAND identity | Conventional technical term | Responsibility | Current status |
|---|---|---|---|
| SAND Core | Kernel core | Boot handoff, CPU control, memory, scheduling, processes and privilege boundaries | Early x86 implementation |
| SAND DeviceCore | Device model / driver framework | Device registration, capabilities, lifecycle and driver contracts | Planned; block-device registry exists |
| SAND Lumen | Display subsystem | Display discovery, mode information and framebuffer backend | Early VBE framebuffer backend exists |
| SAND Gesture | Input subsystem | Normalized keyboard, pointer and later touch events | PS/2 polling prototype only |
| SAND Canvas | Graphics engine | Primitives, fonts, surfaces and drawing API | Framebuffer primitives exist |
| SAND Prism | Compositor | Surface composition, clipping, damage tracking and presentation | Not implemented |
| SAND Aether | Window manager | Window lifecycle, focus, z-order, placement and workspaces | Small kernel prototype; not interactive desktop-ready |
| SAND Horizon | Desktop environment | Shell, panel, launcher, settings and workspace experience | Static boot desktop prototype |
| SAND Terra | Native filesystem | Persistent files, directories, metadata and mount semantics | SANDFS v1 prototype |
| SAND Resonance | Audio subsystem | Audio device and stream interfaces | Not implemented |
| SAND Terminal | Command shell | Interactive command interpreter and system tools | Not implemented as a full shell |

## Names versus API symbols

The source API should stay explicit and stable. Example mapping:

- Architecture identity: SAND Veyra
- Subsystem identity: SAND Lumen
- Technical API: display device and framebuffer interfaces
- Driver path: `kernel/arch/x86/vbe.c`
- User-facing label: Display

Likewise, SAND Aether is the window-management subsystem; functions may still be named `window_create`, `window_close` and `window_get`.

## Service naming

When SAND gains a service manager, use names such as `lumen`, `gesture`, `prism` and `horizon` for service identifiers. Do not describe kernel code as a service unless it has a real service lifecycle and isolation boundary.

## Change control

New official names or changes to this table require updating this document and the API catalogue. A named subsystem is not a completed implementation or an ABI guarantee.
