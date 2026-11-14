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
| Kernel threads | Timer-driven round-robin kernel-thread scheduler, blocking/wakeup, semaphore and mutex prototypes; user processes run as scheduler threads | Fixed thread/stack pools (10 threads, 8 KiB stacks); single CPU |
| User mode / syscalls | Ring-3 entry, TSS kernel stack, DPL-3 `int 0x80` gate, 25 syscalls (ABI v5 in `sdk/sapp/include/sand/abi.h`) with user-pointer validation | Development ABI; not yet declared stable |
| Processes | Separate address spaces, ELF32/SAPP v1 loading, activation, termination, exit status and wait primitives | **M7 complete:** scheduler-owned process contexts, per-process kernel stacks, CR3/TSS switching, exit/wait/spawn, ring-3 fault containment (exit status `-256-vector`), orphan reaping, up to 6 processes |
| Drivers | Device registry, 16550 serial, IRQ-driven PS/2 keyboard and mouse, generic IRQ routing, 4-position PATA ATA PIO, CMOS RTC (**M8**) | Emulator-tested only; no USB, AHCI or APIC |
| Storage | Block-device abstraction, ATA PIO and SANDFS v1 (volume at LBA 1024) format/mount/read/write/list/remove | Experimental one-sector-per-file filesystem; not suitable for important data |
| Native apps | Windowed `locus` (files), `quanta` (calculator), `chronos` (stopwatch), the `cairn` terminal window (sand-apps catalogue), a `lumen` pixel-buffer demo, the `keystone` system-settings app (accent colour, reduced motion, 12/24-hour clock; persisted to `system.cfg` in SANDFS), console `locusc`, plus `hello`, `abitest`, `spin`, `crash`; embedded in the kernel image | Not loaded from disk; other sand-apps are still host-side prototypes; start windowed apps from the dock launcher (generated from manifests) or with `open <app>` in the shell |
| Shell | Native ring-3 `sandsh` (line editing, ls/cat/write/rm, run/ps/mem/devices, app launcher), supervised and restarted by the kernel (**M9**) | Runs over serial and in the desktop console window; no pipes, scripting or job control |
| Graphics | **SAND Dusk** desktop with a **window service** (**M11**): original stroke typeface *SAND Sans*, anti-aliased shape/shadow renderer, procedural twilight-dune wallpaper, menu bar with RTC clock, floating dock, frosted-glass windows (blurred-wallpaper title bars, dune-ridge title line, original "horizon" controls - ripple/rising-sun/setting-sun glyphs with hover bloom - and focus glow), dock with a sun-ray app launcher, pointer, focus-aware keyboard routing, system-console window, About panel, and a window service: apps create a surface and paint it with SAND draw commands (ABI v5, syscalls 18-25; up to 3 app windows, 520x380 max), with 160 ms fade-and-rise open/close animation, opt-in drag-to-resize (dune-grip handle, `SAND_EV_RESIZE`) and app-owned pixel buffers (`win_blit`, optional 1-4x upscale; demo app LUMEN) | Draw-command renderer plus pixel blits, not a GPU API; no maximise, no wheel input, animations only for app windows; app windows are not permission-gated yet; 1024x768 VBE only |
| Applications | SAPP SDK and development package loader | Current kernel loader accepts unsigned SAPP v1; no native STCI/Ed25519 enforcement yet |
| Security / trust | Veyra ABI and STCI contracts, trust-model documentation and Ed25519 known-answer fixture | Native in-house SHA-256/SHA-512/Ed25519 verifier: SAPP v2 signature, publisher certificate chain to a provisioned root, validity window, revocation, key-usage policy; fail-closed launch gate in the loader (`process_create_sapp`). Verified by the STCI known-answer vector, a ~150-case host matrix (incl. images signed by the real sand-apps tools) and a boot self-test. Honest limits: the default policy is DEVELOPER (unsigned v1 apps still run, audited) and the built-in trust store has no roots — production roots/`REQUIRE_SIGNED` must come from trusted boot configuration, which does not exist yet; the PC RTC is not a trusted clock; no attestation/portal |

Implementation claims are tied to source and automated test evidence. A documented interface, test fixture, roadmap item, or host-side tool is not by itself a working kernel feature.

## Current engineering focus: M11/M12 desktop apps

M7-M9 are complete and M11's window service is in: apps can open windows, paint them, and receive routed keyboard and pointer events (`tests/qemu/test_desktop.py`). Next:

- Port more sand-apps onto the window service; QUANTA, CHRONOS, KEYSTONE and the CAIRN terminal window are done.
- The launcher catalogue is generated at build time from each app's `manifest.json` (`launcher` object); apps are still embedded in the kernel image, so the catalogue cannot change without a rebuild until apps load from SANDFS.
- Resizable windows and app-owned pixel surfaces (shared memory) for richer apps.
- Load apps from SANDFS instead of embedding them in the kernel image.

The native Ed25519 primitive and SAPP v2 transcript check exist (`kernel/crypto`, `kernel/security/stci.c`); the certificate chain, trust store and launch gate remain to do. Unsigned SAPP v1 packages are still development-only.

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
