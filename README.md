# SAND OS

**SAND OS** is an independent operating-system research project. **SAND** is its current development codename; the final product name remains undecided.

The goal is to build a general-purpose operating system from the ground up: our own bootloader, kernel, memory management, scheduler, system-call interface, drivers, filesystem, shell, graphics stack, and native applications.

> No Linux kernel, no Linux distribution, and no pre-existing general-purpose OS as the resulting runtime foundation.

QEMU is used only as an external machine emulator for development and automated testing. It is not included in the OS image or runtime. The compiler, assembler, linker, Make, and other host utilities are development tools. The BIOS firmware interface is part of the target machine platform; SAND implements its own boot stages and does not use GRUB.

## Current status

**M0 — custom BIOS boot chain bring-up.** The repository now contains a SAND-authored 16-bit BIOS boot sector and second-stage loader. The first stage reads stage two using BIOS INT 13h extensions; stage two loads a fixed-size raw kernel payload, enables A20, installs a flat GDT, enters 32-bit protected mode, and transfers control to the kernel.

This is an early legacy-BIOS x86 bring-up path, not a finished or broadly hardware-compatible bootloader. It currently loads a bounded raw kernel image and does not yet parse ELF program headers, discover memory maps, support UEFI, or enter 64-bit long mode. The current kernel is 32-bit.

## Build and run

On Linux or WSL with GCC, GNU Binutils, GNU Make, and QEMU System i386:

```sh
make clean
make check
make run
```

The output is `build/sand-os.img`, a raw disk image. QEMU boots this image as a virtual hard disk. No GRUB tooling, GRUB configuration, ISO builder, or Multiboot protocol is used in this boot path.

See [docs/BUILDING.md](docs/BUILDING.md) and [docs/ROADMAP.md](docs/ROADMAP.md).

## License

This repository uses the custom **SAND OS Restricted Source License v1.0** in [LICENSE](LICENSE). It is source-available, not OSI-approved. Third-party tools retain their own licenses.

Repository: [jadhavdurvesh/sand-os](https://github.com/jadhavdurvesh/sand-os)
