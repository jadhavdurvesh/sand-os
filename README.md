# SAND OS

**SAND OS** is an independent operating-system research and engineering project. It is being built as a native operating system: its own boot path, kernel, memory manager, scheduler, process model, drivers, filesystem, system-call ABI, graphics stack, shell, and applications.

The project is developed under the SAND codename. The final product name is not yet fixed.

> **Platform rule:** SAND is not a Linux distribution and does not use the Linux kernel or another general-purpose OS as its runtime foundation. QEMU is used only as an external emulator for development and automated testing. Host compilers, assemblers, linkers, Make, Python test tools, and QEMU are development tools—not components of the installed OS.

## Current implementation status

SAND is beyond a boot-sector-only prototype. The repository contains a working 32-bit legacy-BIOS boot path and an expanding freestanding kernel. The table below distinguishes implemented foundations from incomplete subsystems.

| Area | Current state | Important boundary |
|---|---|---|
| Boot | Custom 16-bit BIOS boot sector and 8 KiB second stage; raw kernel load; A20, GDT and protected-mode transition | Legacy BIOS, fixed LBAs and bounded raw image; no UEFI or long mode |
| CPU / interrupts | GDT, IDT, exception stubs, PIC and PIT | Early x86 implementation; no SMP/APIC support |
| Memory | E820 discovery, bounded physical-frame allocator, 32-bit paging, per-process page-directory creation, small kernel heap | First 16 MiB identity-mapped; no demand paging, swap or mature VM subsystem |
| Kernel threads | Timer-driven round-robin kernel-thread scheduler, blocking/wakeup, semaphore and mutex prototypes | Fixed thread/stack pools; single CPU; kernel threads only |
| User mode / syscalls | Ring-3 entry, TSS kernel stack and DPL-3 syscall gate | Minimal proof-of-concept syscall dispatcher, not a stable general-purpose ABI |
| Processes | Separate address spaces, ELF32/SAPP v1 loading, activation, termination, exit status and wait primitives | **M7 is in progress:** user processes do not yet have complete scheduler-managed execution contexts and preemptive multi-process execution |
| Storage | Block-device abstraction, primary-master ATA PIO and SANDFS v1 format/mount/read/write | Experimental one-sector-per-file filesystem; not suitable for important data |
| Graphics | VBE framebuffer setup, drawing primitives, initial desktop rendering and window structures | Static kernel-rendered desktop; no complete compositor, input routing or interactive shell |
| Applications | SAPP SDK and development package loader | Current kernel loader accepts unsigned SAPP v1; no native STCI/Ed25519 enforcement yet |
| Security / trust | Veyra ABI and STCI contracts, trust-model documentation and Ed25519 known-answer fixture | Contract and design work only; kernel verifier, protected trust store and fail-closed SAPP v2 launch gate are not implemented |

Implementation claims are tied to source and automated test evidence. A documented interface, test fixture, roadmap item, or host-side tool is not by itself a working kernel feature.

## Current engineering focus: M7 process execution

The immediate milestone is to turn process loading and address-space management into a real execution model:

- Give each runnable user process a scheduler-owned execution context and kernel stack.
- Preserve and restore the complete interrupt-return context across timer preemption.
- Switch address spaces and the TSS ring-0 stack together when a process is scheduled.
- Add a defined process-exit syscall path, parent/child wait semantics and safe resource reclamation.
- Test multiple isolated user programs, timer preemption, blocking wait, exit status and cleanup under QEMU.

The current ring-3 test program is a privilege-boundary smoke test, not evidence that concurrent user applications are already supported.

## In-house DMJ technology strategy

SAND will prefer suitable in-house DMJ technologies, but integration must respect kernel boundaries, dependency cost, licensing, resource limits and security review. A DMJ repository is not automatically part of the OS simply because it is owned by the same group.

The current integration map is maintained in [docs/DMJ_TECHNOLOGY_INTEGRATION.md](docs/DMJ_TECHNOLOGY_INTEGRATION.md). Examples include:

- **AETHER / ARGUS:** host-side executable inspection and formal policy checks in build and CI workflows.
- **OCTET / FROSTBYTE:** bounded message encoding and stream framing after SAND has stable IPC/transport contracts.
- **CONTRACT / PHANTOM / ORBIT:** optional user-space service, event and job orchestration layers after process isolation and IPC are mature.
- **DATAGRAM / CALLGRAPH:** user-space networking and typed remote operations after native network drivers and transport APIs exist.
- **EREBUS:** network policy/filter integration after the native network stack exists.
- **Citadel / NOCTIS / MAINTAIN AI / DMJScript / LEXORA / Relay / DMJ LLM:** user-space desktop, workflow and assistant applications after the native application/service APIs are ready.

The kernel remains the authority for memory, scheduling, device ownership and capabilities. General-purpose frameworks, AI runtimes, network services and orchestration systems do not belong in ring 0. Where a DMJ technology is unsuitable or not yet ready, SAND will use a small native implementation or a documented adapter boundary rather than forcing an unsafe dependency.

## Build and run

On Linux or WSL with GCC's 32-bit freestanding support, GNU Binutils, GNU Make and QEMU System i386:

```sh
make clean
make check
make run
```

The build produces `build/sand-os.img`, a raw disk image for the legacy-BIOS QEMU machine. See [docs/BUILDING.md](docs/BUILDING.md), [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## Development principles

1. Build the OS itself; do not substitute another OS kernel or runtime.
2. Keep architecture code, kernel core, drivers, storage, userland and graphics separated.
3. Define stable, versioned interfaces before integrating independent DMJ projects.
4. Validate untrusted executable/package input before allocating or mapping it.
5. Prefer fail-closed security behavior; never treat an unsigned package as trusted.
6. Add a reproducible test with each subsystem change. Compilation alone does not complete a milestone.
7. Report unsupported hardware and incomplete features honestly.

## License

This repository uses the custom **SAND OS Restricted Source License v1.0** in [LICENSE](LICENSE). It is source-available and is not OSI-approved. Third-party tools retain their own licenses.

Repository: [jadhavdurvesh/sand-os](https://github.com/jadhavdurvesh/sand-os)
