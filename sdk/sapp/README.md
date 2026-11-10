# SAND Application SDK — SAPP v0.1

This directory is the first developer-facing SAND application SDK. It defines
the native SAPP executable container and provides a host-side packer/validator,
an initial i386 userspace header, a linker script and a hello-world project.

## Requirements

- Python 3.9+
- An i386 bare-metal C toolchain: `i686-elf-gcc`, `i686-elf-ld`,
  `i686-elf-objcopy`
- GNU Make

## Build an application

From `sdk/sapp/examples/hello`:

```sh
make
make inspect
```

If your cross-compiler uses different command names:

```sh
make CC=i686-linux-gnu-gcc LD=i686-linux-gnu-ld OBJCOPY=i686-linux-gnu-objcopy
```

The output is `build/hello.sapp`. Copy `sdk/sapp` into another application
repository or use it as a Git submodule. The tool is independent of the SAND OS
kernel build.

## Packer commands

```sh
python3 tools/sapp.py pack --input app.bin --manifest manifest.json --output app.sapp
python3 tools/sapp.py verify app.sapp
python3 tools/sapp.py info app.sapp
```

The packer reads static ELF32 i386 program headers and emits multiple SAPP
segments, preserving BSS sizes and R/W/X permissions. It can also include
explicit SAND_ABS32 relocation records from an optional JSON relocation file.
The kernel loader validates and maps segments, zero-fills BSS and applies those
relocations. Shared libraries, cryptographic signatures and package installation
remain follow-on work.

## Current compatibility status

SAPP v0.1 is a real, custom container with its own magic, header, segment table,
metadata and validator. The kernel does **not yet** load SAPP. The existing
kernel process tests still use the temporary ELF32 test loader. Applications
can be authored and packaged now, but cannot yet be executed by SAND OS until
the native kernel loader and matching syscall ABI are integrated and tested.
The userspace syscall declarations document the intended ABI; not every call
is implemented in the kernel yet.
