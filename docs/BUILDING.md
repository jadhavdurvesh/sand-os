# Building and running SAND OS

## CI build

Push to `main` or open a pull request. GitHub Actions installs host build tools, compiles the freestanding kernel, validates its Multiboot2 header, creates a bootable ISO, boots QEMU, captures a screenshot, and uploads the outputs as workflow artifacts.

## Local prerequisites

A Linux or WSL development host with:
- GCC with 32-bit freestanding support for this initial entry path
- GNU Binutils
- GNU Make
- GRUB utilities (`grub-file` and `grub-mkrescue`)
- xorriso
- QEMU System i386

These are host-side development tools. They are not the SAND OS runtime.

## Commands

```sh
make clean
make check
make iso
make run
```

The first validation kernel enters in 32-bit protected mode. x86_64 is the intended platform, but the long-mode transition and 64-bit kernel ABI are future work and must not be considered implemented yet.
