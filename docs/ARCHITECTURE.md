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

## M6: native block storage (SANDFS v1, replaced by MIMIR)

The first storage stack has a SAND-owned block-device registration interface with bounded device slots and sector-based read/write callbacks. The initial x86 driver implements primary-master ATA PIO using IDENTIFY DEVICE and 28-bit LBA single-sector transfers. It polls device status with finite timeouts and exposes the detected sector count through the block-device descriptor.

SANDFS v1 is a deliberately small, versioned, one-sector-per-file prototype. Its superblock records a magic value, format version, volume size, directory location, and data-region start. A fixed directory contains up to 15 named entries; each entry stores a bounded filename, absolute data LBA, and byte length. Format and mount validate device bounds and metadata. The boot test formats a reserved disk range beginning at LBA 512, mounts it, writes a test file, reads it back, and compares every byte.

**Update:** SANDFS v1 has been replaced by MIMIR (see [MIMIR.md](MIMIR.md)): content-addressed chunks, immutable versioned records, a hash chain sealed by alternating seals, twin log tides and Veyra permission labels. The paragraph above describes the retired prototype and its boot test; the boot test now formats and mounts a MIMIR volume at LBA 2112 and prints `SAND_TEST:M6_MIMIR_FORMAT_PASS`.

Current limitations of the storage stack: ATA support is primary-master PATA/IDE only, polling only, single-sector and 28-bit LBA; there is no interrupt-driven I/O, DMA, SATA/AHCI, secondary channel, partition discovery, removable media, or device hotplug. SANDFS supports only 15 entries and contiguous files up to 64 KiB (whole-file FS syscalls, BEDROCK; the old 512-byte syscall limit is gone), has no directories, fragmentation handling, free-space bitmap, journaling, crash consistency, checksums, permissions, or concurrent access locks. The filesystem is an experimental on-disk format and must not be used for important data. The test formats only a dedicated reserved region of the emulator disk, not the bootloader/kernel region.

## Graphics stack (M11)

`font` (SAND Sans stroke typeface, rasterised once into coverage bitmaps) → `gfx` (anti-aliased rects, rounded rects, circles, lines, shadows, text; every primitive honours a clip) → `ui` (wallpaper cache, windows, dock, menu bar, pointer, console surface; dirty-rectangle repaint into a 32-bit back buffer) → `desktop` (kernel thread: polls PS/2 pointer events, presents the damaged rectangle to the 24/32-bpp VBE framebuffer). `ui` takes all system data through a small host-hook struct, so it also builds natively for previews. User applications do not draw here yet; a window service is the next M11 step.

### Window service (ABI v4)

`SAND_SYS_WIN_CREATE/DRAW/EVENT/CLOSE` (18-21). A window is a kernel-owned surface (pool of 3, up to 520x380) that the owning process paints by submitting batches of up to 64 fixed-size draw commands (clear, rect, rounded rect with optional gradient, ring, circle, line, shadow, text in the SAND typeface). The desktop thread composites the surface into frosted-glass chrome and routes pointer and keyboard events to the focused window through a 16-entry per-window queue (mouse-move events coalesce). Ownership is checked by pid on every call; a window is destroyed when its owner exits or the user closes it, after which calls return `SAND_E_NOENT`. Keyboard routing moved out of the PS/2 IRQ path: the desktop decides whether a key goes to the console or to the focused app window.

Window controls are SAND's own "horizon" set, drawn as vector glyphs on the right of the title bar: a dune ripple (minimise to the dock), a rising sun (centre the window), a setting sun (close). They are bare strokes at rest and bloom into a tinted halo on hover; the left of the title bar carries a small accent "sun" that identifies the window. "Centre" is deliberate: shell windows and app surfaces have fixed sizes, so there is no maximise yet.

The dock starts with a launcher tile (a sun with eight rays). It opens a glass panel listing the windowed apps; choosing one asks the kernel to start it via `process_spawn_detached`, which gives the child no parent so it is reaped automatically when it exits. The panel's list is a catalogue generated at build time by `tools/gen_app_table.py` from each app's `manifest.json`: an app appears only if its manifest has a `launcher` object (`label`, `subtitle`, `accent`/`accent2` gradient colours as `#RRGGBB`, one-character `glyph`, `order`). The generator validates every field and fails the build on a malformed manifest; the compositor reads the catalogue through host hooks, so it stays independent of the app table. Until apps load from SANDFS the catalogue is fixed at build time.

### TIDEWAY message passing (ABI v8)
`kernel/ipc/tideway.c` is SAND's own IPC: up to 8 named channels (`[a-z0-9._-]`, at most 15 characters), each a FIFO of 8 messages of 1..112 bytes. Syscalls 29-31 (`CHAN_OPEN`, `CHAN_SEND`, `CHAN_RECV`) need the `ipc.channel` manifest permission (bit 0x100; unlisted or jailed apps get `SAND_E_PERM` and an audit line). Open is idempotent by name; receive is non-blocking (0 = none) and delivers the sender pid that the kernel stamped, so a sender cannot forge it. A full channel returns `SAND_E_FULL` (-6). Honest limits: a channel is global by name, so any app holding `ipc.channel` can read or write any channel (it is a capability, not per-pair isolation); channels are never destroyed; there is no blocking receive or wake-up yet; messages are lost on reboot. Tested under QEMU in `abitest` (loopback, ordering, capacity, bad names) and the `jail` probe (denied without the permission).

### HERALD notifications and background service
`apps/herald` is a window-less SAPP (permission `ipc.channel` only) that holds timers for other apps; it is a TIDEWAY service, not a new syscall. Clients send `R<seconds> <text>` (1 s to 24 h, up to 8 pending, text up to 60 characters), `C` (cancel all) or `Q` (list pending) on channel `herald.req`; replies come on `herald.rep`. When a timer is due the service posts the text to `herald.notify`. The desktop thread reads that channel itself (kernel side, interrupts off while it touches the queue) and draws a toast card under the menu bar for 5 seconds (`sand_ui_toast`, one at a time, glass card with the accent sun). The card's first line is the **kernel-stamped name of the sending process**, so an app cannot pose as another, but any app holding `ipc.channel` can post a toast with its own name on it. CITADEL starts HERALD on demand (it holds `process.spawn`), sends reminders to it, and re-reads the pending list on start, so reminders now survive closing the CITADEL window (the service is orphaned, not killed). Limits: the service and its timers are not persistent across reboot, one toast shows at a time (others queue in the channel, at most 8), there is no click/dismiss action, and the timers use the kernel tick (no wall-clock alarms).

### Citadel assistant (ABI v7)
`apps/citadel` is a window app that runs the Citadel core and its SandOS adapter (vendored from the Citadel repository into `third_party/citadel` by `tools/sync_citadel.py`, which records the source commit in `VERSION`). Citadel is deterministic phrase matching, not a language model: "open locus", "apps", "what time is it", "status", "uptime", "show settings", and "set accent|motion|clock ...", "note <text>", "notes", "clear notes" (asks y/n), "remind me in <n> seconds|minutes|hours [to <text>]", "reminders", "clear reminders" are the supported requests. Notes (max 8, 60 characters each, 500 bytes total by choice) persist in `notes.txt` on SANDFS; reminders (max 4, up to 24 h) are handed to the HERALD background service, so they still fire as a desktop toast after the window is closed (and a "Reminder:" row plus amber ring show while it is open); they do not survive a reboot (`help` lists them). The app supplies only a platform table that maps these to syscalls: `APP_LIST`/`SPAWN` (it lists and starts only user-facing apps, never the shell or the test/probe apps), `SETTING_GET/SET`, `SYSINFO`, and the new `TIME_GET` (syscall 28: the machine's real-time clock as set by firmware, UTC-unsynchronised; read-only, open to every app). Changing a setting never runs on the first request: Citadel's policy requires confirmation, the app asks and runs it only after `y`. Manifest permissions: `window.create`, `process.spawn`, `settings.write`, `fs.rw`, `ipc.channel`. Limits: a child app gets its own manifest's permissions, so Citadel can open any listed app; the clock is unsynchronised; the assistant does not understand free-form language, and answers are English only; the Citadel code is `-Werror` clean but its tests live in the Citadel repository (fake platform), here it is tested under QEMU only (test_09b, test_09c).

### Installing apps (ABI v6)
Syscalls 26/27 (`INSTALL_PUT`, `INSTALL_COMMIT`, permission `system.install`) stage an image in a kernel buffer (append-only, one owning process at a time, 64 KiB max) and store it as `<name>.sapp`. Commit refuses invalid names (`[a-z][a-z0-9]{0,15}`), the names of built-in apps (a disk app can never shadow one), data that is not a SAPP image, and anything the launch gate would deny (SAPP v2 chain failures; unsigned v1 under REQUIRE_SIGNED); it logs `SAND_AUDIT:APP_INSTALLED` or `INSTALL_DENIED_BY_GATE`. The gate runs again at every launch. The shell command `install <name>` takes the image as hex lines (<=100 bytes per line, each acknowledged with `.`) ended by `end`; `tools/sand_install.py` drives it from the host: `--connect HOST:PORT` talks to a running system (`make run-serial` exposes the console on TCP 4555), `--boot IMAGE --out NEW.img` boots a headless copy, installs, optionally runs the app, shuts down cleanly and writes the modified image; exit status 0 installed, 1 refused, 2 usage/connection error (tests/qemu/test_install_tool.py). Limits: in the default developer profile any unsigned SAPP can be installed (and declares its own permissions), so this is a convenience, not a security boundary, until REQUIRE_SIGNED and provisioned roots exist; installed apps appear in the launcher only after a reboot; the helper sends about 3 KB per second over the serial console (a 64 KiB app takes minutes); only the shell holds `system.install`.

### Apps on disk (MIMIR)
MIMIR stores each app as a file of up to 256 KiB (up to 32 files per volume; `.sapp` files carry write label `system.install`). `tools/mkmimir.py` writes a volume into the disk image at build time (`DISK_APPS` in the Makefile, currently `jail` and `lumen`); the kernel mounts it as before. `spawn` tries the built-in table first, then `<name>.sapp` in SANDFS (`kernel/proc/diskapps.c`), so a disk file can never shadow a built-in app. A disk app goes through the same loader and launch gate as any SAPP; its permission bits come from the `permissions` array in its own SAPP metadata (strict parser, unknown names grant nothing, any parse problem grants nothing; host-tested with hostile input under ASan/UBSan). Disk apps may also declare a `launcher` block in their metadata (same field rules as built-in manifests, parsed strictly; invalid blocks are ignored): the desktop scans SANDFS once at start, appends up to 4 such entries after the built-in ones (total capped at 9) and starts them through the same disk path, with the same manifest-derived permissions. The launcher list is fixed after that scan (an app added later needs a reboot). Limits: in developer mode an unsigned (v1) image declares its own permissions, so this is not a security boundary until REQUIRE_SIGNED is on with provisioned roots, where the signature covers the metadata; there is no installer yet (the shell cannot write multi-sector files), disk apps have no launcher entry, and the 256 KiB staging buffer is allocated once from the never-freed kernel heap.

### App permissions (ABI v5)
Each app manifest lists `permissions` (`console.read/write/rw`, `fs.read/write/rw`, `process.spawn`, `window.create`, `settings.write`). `tools/gen_app_table.py` turns them into a bitmask per built-in app (unknown permission names fail the build); `process_create_sapp` stores it in the process, and `syscall_dispatch` refuses a gated call without the bit (`SAND_E_PERM` = -5, audit line `SAND_AUDIT:PERMISSION_DENIED syscall=N`). Gated: console write/read, file read/list/write/remove, spawn, window creation, settings write. Open to every app: ticks, pid, sleep, yield, wait, sysinfo/process/app/device lists, `setting_get`, and calls on a window the app already holds. A process whose name is not in the built-in table gets no bits. Limits: this is capability checking for built-in apps whose manifests are trusted at build time; it does not sandbox ring 3 beyond what paging already does, a spawned child gets its own manifest's bits (not an intersection with the parent's), and permissions for disk-loaded apps will have to come from the signed manifest. Evidence: shell test_10 (the `jail` probe app), generator unit tests, QEMU only.

### System settings (ABI v5)
Syscalls 24/25 (`SETTING_GET`/`SETTING_SET`) read and write a small kernel-owned settings table: accent colour (`SAND_SET_ACCENT`, 5 palettes), reduced motion (skips window fade animation) and 12/24-hour clock. A change repaints the whole desktop. The desktop thread persists the table to `system.cfg` in SANDFS (text `accent=N`, `motion=N`, `clock24=N`) about 50 ticks after the last change and reloads it at boot. The shell `settings` command and the KEYSTONE window are two clients of the same calls. Values are range-checked; any app may currently change them (no permission gating yet).

### Window polish (ABI v4)

- **Animation:** app windows fade in and rise 14 px over 160 ms (ease-out) and fade out on close. The compositor snapshots the backdrop under the window into a lazily allocated scratch buffer and blends the freshly drawn window against it; a closing window stays allocated until the fade finishes and is already invisible to its owner (`WIN_*` return NOENT).
- **Resizing:** an app opts in with `WIN_RESIZABLE(id, min_w<<16|min_h)`. A dune-grip appears at the bottom-right corner; dragging it resizes the surface within [min, 520x380], clears it, and queues `SAND_EV_RESIZE` (x,y = new client size) so the app redraws. Apps that do not opt in keep a fixed size.
- **App-owned pixels:** `WIN_BLIT(id, struct sand_blit*)` copies an app-held 0x00RRGGBB buffer (w*h <= 520*380) into the surface, clipped, with optional integer upscale 1-4x so small SAPP images (64-page limit) can fill a whole window. The kernel validates the user range before copying.
