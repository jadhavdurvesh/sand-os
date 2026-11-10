# SAND Application SDK — SAPP v0.2

The first native SAND application format and developer toolchain. SAPP is a
custom executable container, not ELF renamed with a different extension.

## Requirements

- Python 3.9+
- An i386 bare-metal toolchain: `i686-elf-gcc`, `i686-elf-ld`
- GNU Make

## Build the example

From `sdk/sapp/examples/hello`:

```sh
make
make inspect
```

The output is `build/hello.sapp`. The packer consumes the linked ELF32 file
and emits a native SAPP image. The SDK can be copied into another application
repository or included as a Git submodule.

## Packer

```sh
python3 tools/sapp.py pack --input app.elf --manifest manifest.json --output app.sapp
python3 tools/sapp.py verify app.sapp
python3 tools/sapp.py info app.sapp
```

Optional relocations are supplied as JSON records:

```json
[{"target": 1073745920, "addend": 1073741824}]
```

Use `--relocations relocations.json`. Relocations use the SAND_ABS32 type.

## Implemented in v0.2

- Multi-segment ELF32-to-SAPP packaging.
- BSS represented by memory size greater than file size; loader zero-fills it.
- Per-segment R/W/X permission validation and user page mapping; W+X is rejected.
- Explicit SAND_ABS32 relocation records, validated and applied by the kernel.
- Kernel API `process_create_sapp()` and existing `process_launch()` entry path.

## Not yet production complete

The current target is x86-32 with fixed virtual addresses. The filesystem launch
command, shared-library ABI, dynamic linker, cryptographic signatures and trust
store are not implemented. SAPP images should be treated as development
artifacts until those security and compatibility pieces are complete. The
legacy ELF loader remains temporarily for existing kernel self-tests.
