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
