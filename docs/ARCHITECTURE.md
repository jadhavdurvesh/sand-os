# SAND OS architecture principles

## Intended layering

1. Boot and platform entry
2. Architecture-specific CPU, interrupt, paging and context-switch code
3. Kernel core: memory, scheduler, synchronization, IPC and object model
4. Device framework and hardware drivers
5. Storage and native filesystem
6. System-call ABI and user-mode runtime
7. Native services and shell
8. Graphics, windowing and input
9. Applications, package management and SDK

## Boundary rules

- Architecture-specific code must stay behind interfaces consumed by kernel core.
- Drivers must not bypass kernel-owned resource and memory policies.
- User applications must not execute in kernel privilege level.
- Persistent storage formats must be versioned and validated.
- Every binary format must have a documented specification and parser tests.
- The initial boot protocol is a bring-up dependency, not a permanent architectural commitment.

## Native graphical desktop

SAND OS is intended to provide a graphical desktop, not remain a text-only system. The desktop will be implemented as native SAND components:

1. Display and graphics drivers owned by SAND.
2. A framebuffer and graphics primitives layer.
3. A 2D compositor that combines application surfaces.
4. A window manager for windows, focus, placement, and workspace state.
5. An input subsystem for keyboard, mouse, and later touch.
6. A native desktop shell, launcher, taskbar/panel, settings, and file manager.
7. A documented graphics and windowing API for native applications.

The initial VGA text screen is only a bring-up/debug console. It is not the intended final user interface. A graphical environment depends on stable memory management, interrupt/input handling, scheduling, user-mode isolation, and display drivers; it will be built after those foundations.

## Memory and virtual address space

The initial x86 implementation obtains a BIOS E820 memory map before entering protected mode. The kernel uses a bitmap physical-frame allocator with a deliberately bounded first 16 MiB management window. The first 1 MiB is reserved for firmware and early boot structures.

The first paging milestone uses non-PAE 32-bit paging and identity-maps the first 16 MiB so the currently executing kernel, stack, VGA text buffer, and early data remain reachable when CR0.PG is enabled. A small kernel heap reserves virtual addresses beginning at 16 MiB and maps physical frames on demand.

Current limitations: this is a bring-up allocator and bump heap, not a production virtual-memory subsystem. The heap has no free operation; page permissions are supervisor read/write; there is no address-space isolation, unmapping, swapping, demand paging, or memory hotplug. The 16 MiB frame ceiling and identity map will be replaced as the architecture grows.

## Timer and interrupt clock

The initial 32-bit x86 timer uses the programmable interval timer (PIT) channel 0 at a requested 100 Hz. Its IRQ0 entry is installed at IDT vector 32, the master 8259 PIC unmasks only IRQ0, and the handler increments a monotonic 32-bit tick counter before acknowledging the master PIC. The kernel enables interrupts and waits using `hlt` until ten timer ticks have arrived.

This is an early hardware-clock source, not yet a scheduler. The handler does not perform context switches, and the counter will eventually need a wider monotonic time representation and a more capable clock abstraction. Local APIC timer and HPET support are future platform work.

## Kernel threads and preemptive round-robin scheduling

The first scheduler prototype uses four statically allocated kernel-thread slots, each with a 4 KiB stack. A new thread receives a synthetic 32-bit interrupt-return frame and begins at the SAND thread bootstrap routine. The PIT IRQ0 assembly stub saves general-purpose registers, passes the saved stack pointer to the scheduler, then restores the selected thread's stack and returns with `iret`. This provides timer-driven preemption between kernel threads.

This is deliberately an early single-address-space kernel-thread prototype. All threads share kernel privilege and address space. Stacks are static; there is no thread reaper, blocking/wakeup API, priority policy, per-thread FPU state, SMP locking, user mode, or robust idle thread yet. A returning thread is marked dead and yields on a later timer tick. These limitations must be addressed before treating the scheduler as production-ready.

## Blocking synchronization prototype

The M3 extension adds a software reschedule interrupt at vector 48, a BLOCKED thread state, wait-object wakeups, reusable DEAD thread slots, and kernel semaphores and mutexes. The single-core scheduler protects state transitions by disabling maskable interrupts while manipulating its run-state table. A producer-consumer smoke workload exercises blocking and wakeups.

These are experimental kernel-only primitives. The mutex is currently implemented as a binary semaphore without owner tracking, recursion detection, or priority inheritance. Wait queues use a linear scan of a four-slot thread table; thread IDs are not generation-stamped. No SMP support, dynamic stack allocation, robust teardown, deadlock detection, or user-process isolation exists. The boot thread is the always-runnable fallback. This implementation must not be considered production-safe.

## Scheduler validation and idle fallback

The boot context is now explicitly treated as the idle/fallback context: when no worker is runnable, timer or software rescheduling returns to the saved boot-context stack. Kernel thread stacks are drawn from a bounded, statically reserved pool and returned to that pool when a DEAD slot is reused; this is reusable bounded storage, not dynamically allocated per-thread stacks.

The QEMU self-test runs a finite producer-consumer workload. The consumer starts first and blocks on a zero-count semaphore; the producer posts work items; both protect shared counters with the mutex primitive. The test checks exact producer/consumer totals, then waits for both workers to exit and starts a short-lived probe to exercise dead-slot and stack-slot reuse. The debug console emits explicit PASS markers, and CI now fails unless all expected markers appear.

Known constraints remain: this is a uniprocessor, ring-0-only prototype with four thread descriptors, a bounded stack pool, linear wait-object scans, and no ownership-aware mutex, priority inheritance, timeout/cancellation, SMP safety, stack guard pages, or robust fault recovery. The mutex is a binary semaphore wrapper, not a fully featured ownership-tracking mutex.

## M4: first user-mode transition and system-call proof

The x86 GDT now defines kernel and ring-3 code/data segments plus a 32-bit TSS. The TSS supplies the ring-0 stack for privilege transitions. The IDT exposes vector 0x80 as a DPL-3 interrupt gate, and the assembly entry saves the general registers before dispatching a small kernel-owned syscall table.

The current smoke test copies one tiny position-independent user test stub into a dedicated physical frame, maps it at a user virtual address, maps one user stack page, and enters ring 3 with IRET. The stub invokes syscall number 1 with a fixed test token. CI requires the user-mode entry and successful syscall markers before reporting PASS.

This is a privilege-boundary proof of concept, not yet a general process model. The kernel and test user page still share one page directory; there are no per-process address spaces, process descriptors, user ELF loading, safe user-pointer validation, syscall argument copying, preemptive user-process scheduling, exit/wait semantics, or fault isolation/recovery. The syscall dispatcher accepts only the fixed test operation and must not be treated as a usable general-purpose ABI. The user test stub intentionally loops after returning from the syscall.

## M5: independent process address spaces and lifecycle

The first process manager introduces a bounded process table with process IDs, names, READY/RUNNING/TERMINATED states, independent page-directory frames, a private user-code page, and a private user-stack page. Process creation copies supervisor-only kernel page-directory mappings into each process directory while leaving the user virtual range private. User mappings are accepted only in the reserved user range and require the user permission bit. Activating a process switches CR3; terminating the active process switches back to the kernel directory; destruction reclaims the process's user page tables, code/stack frames, and page-directory frame.

The boot self-test creates two processes, checks that their page-directory physical addresses differ, switches between their address spaces, terminates and destroys one, then enters ring 3 in the remaining process and verifies that its syscall is accepted only while a process is active.

This is not yet a complete process scheduler. Processes do not yet have saved user register contexts, a scheduler queue, independent kernel stacks, fork/exec, a user ELF loader, wait/exit syscalls, or safe user-pointer copying. Process activation is currently a kernel-controlled CR3 operation used by the boot test; it is not a preemptive user-process context switch. The current test process loops after its syscall. Process slots are statically bounded, and SMP concurrency is not supported.

## M6: native block storage and SANDFS v1

The first storage stack has a SAND-owned block-device registration interface with bounded device slots and sector-based read/write callbacks. The initial x86 driver implements primary-master ATA PIO using IDENTIFY DEVICE and 28-bit LBA single-sector transfers. It polls device status with finite timeouts and exposes the detected sector count through the block-device descriptor.

SANDFS v1 is a deliberately small, versioned, one-sector-per-file prototype. Its superblock records a magic value, format version, volume size, directory location, and data-region start. A fixed directory contains up to 15 named entries; each entry stores a bounded filename, absolute data LBA, and byte length. Format and mount validate device bounds and metadata. The boot test formats a reserved disk range beginning at LBA 512, mounts it, writes a test file, reads it back, and compares every byte.

Current limitations: ATA support is primary-master PATA/IDE only, polling only, single-sector and 28-bit LBA; there is no interrupt-driven I/O, DMA, SATA/AHCI, secondary channel, partition discovery, removable media, or device hotplug. SANDFS supports only 15 entries and files up to 512 bytes, has no directories, multi-sector files, free-space bitmap, journaling, crash consistency, checksums, permissions, or concurrent access locks. The filesystem is an experimental on-disk format and must not be used for important data. The test formats only a dedicated reserved region of the emulator disk, not the bootloader/kernel region.

## Graphics stack (M11)

`font` (SAND Sans stroke typeface, rasterised once into coverage bitmaps) → `gfx` (anti-aliased rects, rounded rects, circles, lines, shadows, text; every primitive honours a clip) → `ui` (wallpaper cache, windows, dock, menu bar, pointer, console surface; dirty-rectangle repaint into a 32-bit back buffer) → `desktop` (kernel thread: polls PS/2 pointer events, presents the damaged rectangle to the 24/32-bpp VBE framebuffer). `ui` takes all system data through a small host-hook struct, so it also builds natively for previews. User applications do not draw here yet; a window service is the next M11 step.

### Window service (ABI v3)

`SAND_SYS_WIN_CREATE/DRAW/EVENT/CLOSE` (18-21). A window is a kernel-owned surface (pool of 3, up to 520x380) that the owning process paints by submitting batches of up to 64 fixed-size draw commands (clear, rect, rounded rect with optional gradient, ring, circle, line, shadow, text in the SAND typeface). The desktop thread composites the surface into frosted-glass chrome and routes pointer and keyboard events to the focused window through a 16-entry per-window queue (mouse-move events coalesce). Ownership is checked by pid on every call; a window is destroyed when its owner exits or the user closes it, after which calls return `SAND_E_NOENT`. Keyboard routing moved out of the PS/2 IRQ path: the desktop decides whether a key goes to the console or to the focused app window.

Window controls are SAND's own "horizon" set, drawn as vector glyphs on the right of the title bar: a dune ripple (minimise to the dock), a rising sun (centre the window), a setting sun (close). They are bare strokes at rest and bloom into a tinted halo on hover; the left of the title bar carries a small accent "sun" that identifies the window. "Centre" is deliberate: shell windows and app surfaces have fixed sizes, so there is no maximise yet.

The dock starts with a launcher tile (a sun with eight rays). It opens a glass panel listing the windowed apps; choosing one asks the kernel to start it via `process_spawn_detached`, which gives the child no parent so it is reaped automatically when it exits. The panel's app list is a fixed table in `ui.c` today.
