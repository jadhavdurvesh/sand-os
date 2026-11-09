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
