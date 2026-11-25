# SAND OS engineering roadmap

| Milestone | Scope | Exit evidence |
|---|---|---|
| M0 | Custom BIOS boot sector, stage two, raw kernel loading | SAND loader reaches kernel entry in QEMU |
| M1 | x86 architecture foundation: GDT, IDT, exception stubs, masked PIC remap | Controlled INT3 test and exception panic path |
| M2 | BIOS E820 discovery, bounded physical-frame allocator, 32-bit identity paging, page-backed bump heap | QEMU memory-map, frame alloc/free, paging and heap read/write smoke tests |
| M3 | PIT IRQ0, round-robin kernel threads, blocking/wakeup, semaphore/mutex primitives, idle fallback and bounded stack-slot reuse | QEMU producer-consumer totals, mutex-protected counter, worker termination and stack-slot reuse PASS markers |
| M4 | Ring-3 privilege transition, kernel TSS stack, DPL-3 syscall gate and first user syscall | QEMU enters ring 3 and the user stub completes syscall 1 |
| M5 | Independent process control blocks, per-process page directories, activation, termination and destruction | Two distinct address spaces, CR3 switching, lifecycle and user-process syscall smoke tests |
| M6 | Block-device abstraction, primary-master ATA PIO, SANDFS v1 format/mount/read/write (later replaced by MIMIR) | ATA IDENTIFY and filesystem write/read/byte-compare tests |
| M7 ✅ | Complete user-process execution: scheduler-owned process contexts, per-process kernel stacks, timer preemption across user contexts, CR3/TSS switching, syscall exit, parent/child wait and deterministic cleanup | QEMU runs at least two isolated ring-3 programs under timer preemption; each exits with a checked status; parent blocks/wakes; address spaces and kernel stacks are reclaimed; invalid user state is contained |
| M8 ✅ | Device model and drivers: serial, keyboard, display, storage expansion | Driver-level hardware and emulator tests |
| M9 ✅ | Native shell and core utilities | Scripted shell tests |
| M10 | Networking foundation and security model, EREBUS adapter groundwork | Protocol, policy-hook and isolation tests |
| M11 🔧 | Graphics compositor, window manager, input stack | Desktop screenshot tests |
| M12 🔧 | Native applications, package format and developer SDK | Application conformance suite |
| M13 | UEFI boot path, hardware bring-up, installers, recovery and release engineering | Hardware compatibility matrix |

## Status and evidence

| Milestone | Status | Evidence in this tree |
|---|---|---|
| M7 | Complete | `userland_selftest` markers, `abitest`, `spin`, `crash` apps; `tests/qemu/test_shell.py` (fault containment, spawn/wait, exit status) |
| M8 | Complete (emulator) | Markers `M8_SERIAL_LOOPBACK_PASS`, `M8_PS2_INIT_PASS`, `M8_IRQ_ROUTING_PASS`, `M8_ATA_BOOT_DISK_PASS`, `M8_STORAGE_SECOND_DISK_DETECTED/READ_PASS`, `M8_DEVICE_REGISTRY_PASS`, `M8_KEYBOARD_IRQ_PASS`, `M8_MOUSE_IRQ_PASS` |
| M9 | Complete | `tests/qemu/test_shell.py` drives `sandsh` over serial |
| M11 | Window service done; compositor features open | Done: font, renderer, wallpaper, frosted-glass chrome, pointer, console window, window service (syscalls 18-21), focus-aware keys (`tests/qemu/test_desktop.py`, incl. windowed LOCUS). Also done: open/close fade animation, opt-in resizing, app-owned pixel buffers with upscale (syscalls 22-23, LUMEN, test_08). Open: maximise, per-window damage rects from apps, window permissions |
| M12 | Started | TIDEWAY IPC (ABI v8: named channels, `ipc.channel`); HERALD background service and desktop toasts (test_09d); HUGINN DNS resolver; DRIFTNET network stack with DHCP and a TCP client (tests/qemu/test_net.py) and Veyra subsystem discovery (tests/veyra/subsystems_test.c); BEDROCK (whole-file FS syscalls); MIMIR (own filesystem: content-addressed chunks, versions, hash-chained seals, twin tides, Veyra labels; ABI v12 syscalls 42-48; tests/fs/mimir_test.c, tests/qemu/test_mimir.py); HEL (undelete: `trash`/`restore`, syscalls 50-51, ABI v14, label-checked and audited; lost on an ebb); BRANCHES (names up to 47 characters, "/" gives a branch structure without directory objects, branch-aware `ls`, `branch/*` wards, `FS_LIST_EX`, ABI v13); SIGIL (Ed25519-signed factory manifest `factory.sigil`: the kernel shows which system files are sealed or changed since the factory image; developer key, factory files only); WARDS (policy file `ward.rules` inside the store: default labels and creation gate for new files, e.g. only `system.install` may create `*.sapp`; `apps/wardprobe`); BIFROST (native AHCI/SATA driver, `kernel/storage/bifrost.c`: PCI discovery, MMIO window at 0xF0000000, polled 48-bit-LBA DMA, registers `sata0`..`sata3` as block devices; boot self-test restores the sector it uses; tests/qemu/test_bifrost.py on QEMU's ICH9 AHCI, boots and stores MIMIR from a SATA disk with no IDE disk present; real hardware untested); GJALLAR (boot score "Gjallarhorn": 4-bit ADPCM after the MIMIR store, Intel HD Audio driver, DAWN paced by the music, key press skips/fades; tests/qemu/test_gjallar.py captures QEMU's audio and compares it with the score); DAWN (boot splash: rising moon, wordmark, progress bar, iris reveal into the desktop; `make dawn-preview`, tests/qemu/test_dawn.py); RIDGELINE (kernel linked and loaded at 1 MiB, up to 1 MiB image).  Windowed native LOCUS, QUANTA and CHRONOS (`apps/`), console `locusc` (test_08), `open <app>` and a dock launcher (test_06/test_07 in test_desktop); sand-apps SDK synced to ABI v6 (run-time `install` command, test_12/13; host helper `tools/sand_install.py`; the CITADEL assistant app with notes and reminders, test_09b/test_09c). Done: native KEYSTONE settings app (accent, reduced motion, 12/24h clock via `SETTING_GET/SET`, persisted to `system.cfg`, test_10 and shell test_09); windowed CAIRN terminal (`apps/cairn`, shares `apps/sandsh/shell_core.h`, test_09). Done: manifest permissions enforced in the syscall dispatcher (`jail` probe, shell test_10). Open: KEYSTONE theme (dark/light) and further settings, full CAIRN command registry natively, an installer and live launcher refresh (the launcher catalogue is manifest-driven for built-in apps, `tools/gen_app_table.py`; spawn already loads `jail`/`lumen` from SANDFS, test_11), signed SAPP v2 enforcement. Done: native SHA-512/Ed25519 verifier + STCI transcript check (`make host-test`, boot marker `STCI_ED25519_KAT_PASS`); certificate chain, revocation, validity window and the loader launch gate (`STCI_LAUNCH_GATE_PASS`); OATH (signed boot profile `boot.oath` in the MIMIR store: policy plus up to 8 trust roots and 8 revocations, Ed25519-signed with the platform key, loaded at boot into the launch gate; invalid fails closed; `make signed-images`, tests/qemu/test_oath.py); not done: production roots, trusted clock, attestation |


The M8 hardware boundary: all driver evidence is from QEMU; nothing is claimed for real hardware.

## Non-negotiable engineering rules

- Do not import or depend on a Linux kernel or another OS runtime.
- Keep bootloader, architecture, kernel core, drivers, filesystem, userland, and graphics in separate modules.
- QEMU is permitted only as an external test environment, not as an OS component.
- Document firmware interfaces and all external development dependencies honestly.
- Add tests with each subsystem; compilation alone does not complete a milestone.
- Never claim hardware support without testing on that hardware.


## M7 execution completion gates

M7 is **in progress**, not complete. The current implementation has process descriptors, independent page directories, ELF32/SAPP v1 image loading, kernel-controlled activation, termination, exit status and a blocking wait primitive. Those pieces do not yet constitute a process scheduler.

The implementation must close these gaps in order:

1. **Context contract:** define one architecture-owned saved interrupt frame for both kernel-thread and user-process scheduling. The frame must account for the privilege-transition stack frame pushed by x86 when an interrupt arrives from ring 3.
2. **Process execution object:** associate each runnable process with a scheduler entity, a private kernel stack, a saved context, an address-space identifier (currently a page-directory physical address), and explicit lifecycle ownership. Do not borrow the boot stack or a kernel worker stack for a user process.
3. **Atomic dispatch:** on a switch, install the selected process's CR3 and TSS ring-0 stack before returning through the selected interrupt frame. Keep the kernel/idle context valid when no user process is runnable. Protect scheduler state transitions from timer/syscall races on the current uniprocessor target.
4. **User entry and return:** create the initial ring-3 interrupt-return frame from validated entry and stack values. Timer preemption must save and later restore the complete user frame. Returning from a syscall must not accidentally restore a different process's address space or kernel stack.
5. **Exit and wait:** provide a defined syscall exit path; wake only the correct parent waiter; preserve exit status until reaped; reject self-wait, unrelated-parent wait, duplicate reap and stale process IDs.
6. **Reclamation:** destroy an address space only after it is no longer active on any CPU and no saved context can resume in it. Reclaim the process kernel stack only after dispatch has switched away from it. Clear slot state atomically and use generation-stamped IDs before slot reuse.
7. **Validation:** add QEMU tests for two independently loaded ring-3 programs, timer-driven switching, register/stack preservation, parent blocking and wakeup, exit status, invalid syscall arguments, repeated create/exit/reap cycles, and memory/slot reuse. CI must assert every marker and retain the serial/debug log and image artifact.

### Native integration constraints during M7

- Keep the scheduler, context frame, page-table and process-lifecycle primitives SAND-owned. Do not put a general-purpose DMJ framework in ring 0.
- Reuse DMJ technologies only behind suitable interfaces. AETHER and ARGUS can assist host-side inspection and policy/state-machine validation; they are not runtime substitutes for kernel checks.
- Keep SAPP parsing separate from cryptographic trust. Until native STCI/Ed25519 verification and a protected trust store are implemented, unsigned development packages must not be described as trusted or production-certified.
- Do not integrate OCTET, FROSTBYTE, CONTRACT, PHANTOM, ORBIT, DATAGRAM, CALLGRAPH, EREBUS or desktop/AI products into the kernel before their documented prerequisites (IPC, service handles, networking, process isolation and capability enforcement) exist.
