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
