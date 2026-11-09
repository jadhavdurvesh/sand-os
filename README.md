# SAND OS

**SAND OS** is an independent operating-system research project. **SAND** is its current development codename; the final product name remains undecided.

The goal is to build a general-purpose operating system from the ground up: our own kernel, memory management, scheduler, system-call interface, drivers, filesystem, shell, graphics stack, and native applications.

> No Linux kernel, no Linux distribution, and no pre-existing general-purpose OS as the resulting runtime foundation.

The build and test environment may use ordinary hosted tools such as a compiler, linker, Make, GRUB tooling for early bootstrapping, and QEMU. These are development dependencies, not components of the resulting operating system.

## Current status

**M0 — repository and boot pipeline foundation.** This initial codebase establishes the project structure and CI path. It is not yet a usable operating system. The initial boot path uses the Multiboot2 protocol as a temporary bring-up mechanism. A fully custom boot path is a later engineering milestone.

The first target is x86_64 hardware, with the initial validation kernel running in 32-bit protected mode under QEMU. Long-mode entry, a 64-bit kernel ABI, and the complete CPU architecture layer are not implemented yet.

## Planned architecture

- Boot: temporary Multiboot2 entry path, followed by a custom boot path.
- Kernel: freestanding C and x86 assembly.
- Target: x86_64.
- Build: freestanding compilation and Make.
- Validation: automated builds and QEMU boot tests in GitHub Actions.
- Evidence: bootable ISO, logs, and screenshot artifacts.

## Repository layout

```text
.
├── boot/                 # Entry code and boot configuration
├── kernel/               # Kernel implementation
├── include/sand/         # Kernel headers
├── linker/               # Kernel linker script
├── docs/                 # Architecture and engineering notes
├── .github/workflows/    # CI and QEMU verification
├── Makefile
├── README.md
├── LICENSE
└── THIRD_PARTY_NOTICES.md
```

## Build and run

The initial CI pipeline is the canonical reproducible build environment. It compiles the freestanding kernel, validates its Multiboot2 header, creates a bootable ISO, starts QEMU, captures a screenshot, and uploads the outputs as workflow artifacts.

See [docs/BUILDING.md](docs/BUILDING.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## Engineering principles

1. Build and own each core OS subsystem instead of adopting a Linux kernel or an existing distribution.
2. Keep subsystems separated behind documented interfaces.
3. Make every milestone testable and reproducible.
4. Treat security, correctness, portability, and maintainability as first-class engineering requirements.
5. Track third-party tools and components with their applicable licenses.

## License

This repository is distributed under the custom **SAND OS Restricted Source License v1.0** in [LICENSE](LICENSE). It grants limited personal, non-commercial evaluation rights. Redistribution, commercial use, and publication of modified versions require prior written permission.

This is a custom source-available license, not an OSI-approved open-source license. Third-party components remain governed by their own licenses.

## Project identity

- Development codename: **SAND**
- Repository: [jadhavdurvesh/sand-os](https://github.com/jadhavdurvesh/sand-os)
- Official product name: undecided
