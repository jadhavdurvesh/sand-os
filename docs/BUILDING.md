# Building and running SAND OS

## Host prerequisites

A Linux or WSL development host with GCC (32-bit freestanding support), GNU Binutils, GNU Make, and QEMU System i386. These are host-side development and testing tools. QEMU is not part of the SAND OS image or runtime. GRUB, Multiboot utilities, xorriso, and ISO creation tools are not required.

## Build and validate

```sh
make clean
make check
```

The build creates `build/sand-boot.bin` (512-byte BIOS sector), `build/sand-stage2.bin` (8 KiB second stage), `build/sand-kernel.elf`, `build/sand-kernel.bin` (bounded flat payload), and `build/sand-os.img` (raw disk image).

## Run in QEMU

```sh
make run
```

QEMU emulates a legacy BIOS PC and boots the raw image as an IDE disk. Firmware BIOS interrupt services perform initial disk reads and text output; SAND's own code controls the boot stages, memory placement, GDT setup, protected-mode transition, and kernel handoff.

## Current limitations

This is an educational first boot path, not a production bootloader. It assumes legacy BIOS with INT 13h extensions, uses fixed disk LBAs and fixed payload limits, and enters 32-bit protected mode. It does not yet parse ELF program headers, obtain a firmware memory map, enter x86_64 long mode, or support UEFI. Do not write this image to a physical disk; use QEMU or a dedicated test device.
