# SAND OS engineering roadmap

| Milestone | Scope | Exit evidence |
|---|---|---|
| M0 | Custom BIOS boot sector, stage two, raw kernel loading | SAND loader reaches kernel entry in QEMU |
| M1 | x86 architecture foundation: GDT, IDT, exception stubs, masked PIC remap | Controlled INT3 test and exception panic path |
| M2 | BIOS E820 discovery, bounded physical-frame allocator, 32-bit identity paging, page-backed bump heap | QEMU memory-map, frame alloc/free, paging and heap read/write smoke tests |
| M3 | PIT IRQ0 timer foundation, kernel threads, scheduler and context switching | Timer tick smoke test, then deterministic thread-switch tests |
| M4 | Kernel object model and system-call ABI | User/kernel boundary tests |
| M5 | Storage abstraction and native filesystem | Filesystem image tests |
| M6 | Device model and drivers: serial, timer, keyboard, block, display | Driver-level QEMU tests |
| M7 | Process model, user mode, executable loader | User-process smoke tests |
| M8 | Native shell and core utilities | Scripted shell tests |
| M9 | Networking foundation and security model | Protocol and isolation tests |
| M10 | Graphics compositor, window manager, input stack | Desktop screenshot tests |
| M11 | Native applications, package format and developer SDK | Application conformance suite |
| M12 | UEFI boot path, hardware bring-up, installers, recovery and release engineering | Hardware compatibility matrix |

## Non-negotiable engineering rules

- Do not import or depend on a Linux kernel or another OS runtime.
- Keep bootloader, architecture, kernel core, drivers, filesystem, userland, and graphics in separate modules.
- QEMU is permitted only as an external test environment, not as an OS component.
- Document firmware interfaces and all external development dependencies honestly.
- Add tests with each subsystem; compilation alone does not complete a milestone.
- Never claim hardware support without testing on that hardware.
