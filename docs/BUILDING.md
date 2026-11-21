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

- RIDGELINE (the kernel above 1 MiB): the second stage reads the kernel in up to sixteen 64 KiB windows (LBA 17-2064) with INT 13h into low memory and has BIOS INT 15h AH=87h copy each window to physical 0x100000, where the kernel is linked (`linker/kernel.ld`); `make` fails if `build/sand-kernel.bin` exceeds 1,048,576 bytes (the kernel is ~508 KB now, about 540 KB of headroom; the image includes the zero-filled .bss). The frame allocator keeps `0x100000..__kernel_end` out of the pool. The boot stack stays at 0x9F000 and the 0x10000-0x8FFFF region is free again. Beyond 1 MiB the loader needs more windows and SANDFS has to move again. `make host-test` and the kernel build need Python 3 and OpenSSL 3 (they generate throw-away test signing keys under `build/`).
- `tools/sync_citadel.py <citadel-checkout>` refreshes the vendored Citadel core in `third_party/citadel/` (commit recorded in `VERSION`).
- `tools/sand_install.py` installs a `.sapp` into a running system (`make run-serial`, then `--connect 127.0.0.1:4555`) or bakes it into a copy of the disk image (`--boot build/sand-os.img --out my.img`); see docs/ARCHITECTURE.md (Installing apps).
- MIMIR (the filesystem, [MIMIR.md](MIMIR.md)) lives at LBA 2112 with 8192 sectors (the disk image is 10,304 sectors, 5,275,648 bytes) so it cannot collide with the kernel image. `make host-test` also builds `tests/fs/mimir_test.c` under ASan/UBSan and mounts the Python-built volume in the image.
- `make test` runs the QEMU-driven suites in `tests/qemu/` (needs Python 3 and `qemu-system-i386`): `test_shell.py` drives `sandsh` over serial, `test_desktop.py` checks desktop markers, wallpaper/chrome pixels, pointer movement and console mirroring from screenshots.
- `make ui-preview` renders one desktop frame natively to `build/ui.ppm` for quick visual iteration (host tool only).
- `tools/genfont.py` regenerates `kernel/graphics/font_data.c` (the SAND Sans glyph tables): `python3 tools/genfont.py emit kernel/graphics/font_data.c`.
