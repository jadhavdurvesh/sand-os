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

## Layout notes (M7–M11)

- The second stage loads up to 384 KiB of kernel (six 64 KiB INT 13h windows, LBA 17-784); `make` fails if `build/sand-kernel.bin` exceeds 393,216 bytes (the kernel is ~385 KB now, so only ~8 KB of headroom remains: the next growth needs a seventh loader window). SANDFS starts at LBA 1024, which caps any further growth at ~500 KiB without moving it.
- SANDFS lives at LBA 1024 so it cannot collide with the kernel image.
- `make test` runs the QEMU-driven suites in `tests/qemu/` (needs Python 3 and `qemu-system-i386`): `test_shell.py` drives `sandsh` over serial, `test_desktop.py` checks desktop markers, wallpaper/chrome pixels, pointer movement and console mirroring from screenshots.
- `make ui-preview` renders one desktop frame natively to `build/ui.ppm` for quick visual iteration (host tool only).
- `tools/genfont.py` regenerates `kernel/graphics/font_data.c` (the SAND Sans glyph tables): `python3 tools/genfont.py emit kernel/graphics/font_data.c`.
