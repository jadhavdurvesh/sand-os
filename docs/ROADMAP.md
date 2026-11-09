# SAND OS engineering roadmap

This roadmap describes intended milestones, not completed functionality.

| Milestone | Scope | Exit evidence |
|---|---|---|
| M0 | Repository, toolchain, boot path, CI evidence | Kernel entry reached in QEMU; screenshot and boot-log artifacts |
| M1 | x86 architecture foundation: GDT, IDT, exceptions, PIC/APIC planning | Exception and interrupt tests |
| M2 | Physical memory map, frame allocator, paging, kernel heap | Allocator and mapping tests |
| M3 | Kernel scheduler, threads, context switching, timers | Deterministic scheduler tests |
| M4 | Kernel object model and system-call ABI | User/kernel boundary tests |
| M5 | Storage abstraction and native filesystem | Filesystem image tests |
| M6 | Device model and drivers: serial, timer, keyboard, block, display | Driver-level QEMU tests |
| M7 | Process model, user mode, executable loader | User-process smoke tests |
| M8 | Native shell and core utilities | Scripted shell tests |
| M9 | Networking foundation and security model | Protocol and isolation tests |
| M10 | Graphics compositor, window manager, input stack | Desktop screenshot tests |
| M11 | Native applications, package format and developer SDK | Application conformance suite |
| M12 | Hardware bring-up, installers, recovery and release engineering | Hardware compatibility matrix |

## Non-negotiable engineering rules

- Do not import or depend on a Linux kernel.
- Keep bootloader, architecture, kernel core, drivers, filesystem, userland, and graphics in separate modules.
- Document every external dependency and its license.
- Add tests with each subsystem; compilation alone does not complete a milestone.
- Never claim hardware support without testing on that hardware.
- Preserve reproducible builds and attach logs/screenshots to CI runs.
