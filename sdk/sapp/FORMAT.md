# SAPP binary format (v0.1)

SAPP is the native SAND application container. It is not ELF with a renamed
extension. The runtime image consists of a fixed 64-byte header, a table of 32-byte load
segments, UTF-8 JSON metadata, an optional table of 12-byte relocations, and
aligned segment payloads.

All integer fields are little-endian. Version 1 is 32-bit x86 (architecture 1).
The format is deliberately versioned and architecture-neutral at the container
level; new architecture identifiers may be added without changing v1 layouts.

## Header: 64 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic: ASCII `SAPP` |
| 4 | 2 | Format version: 1 |
| 6 | 2 | Architecture: 1 = x86-32 |
| 8 | 4 | Header size: 64 |
| 12 | 4 | Flags (must be zero in v1) |
| 16 | 4 | Entry virtual address |
| 20 | 4 | Segment table file offset |
| 24 | 4 | Segment count |
| 28 | 4 | Metadata file offset |
| 32 | 4 | Metadata byte length |
| 36 | 4 | File size |
| 40 | 4 | Signature offset (0 when unsigned) |
| 44 | 4 | Signature length (0 when unsigned) |
| 48 | 4 | Relocation table file offset (0 when empty) |
| 52 | 4 | Relocation count |
| 56 | 8 | Reserved; must be zero |

## Segment record: 32 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Virtual address |
| 4 | 4 | Memory size |
| 8 | 4 | File offset |
| 12 | 4 | File size |
| 16 | 4 | Permissions: R=1, W=2, X=4 |
| 20 | 4 | Alignment (power of two, at least 4096) |
| 24 | 8 | Reserved; must be zero |

Segments may not overlap in virtual memory or in the file. W+X is forbidden.
The entry point must be inside an executable segment. The v1 loader must
validate all ranges before allocating or mapping memory.

## Metadata

Metadata is UTF-8 JSON. Required keys: `id`, `name`, `version`.
Optional keys: `vendor`, `description`, `permissions`, `dependencies`.
Unknown keys are preserved for forward compatibility. Application IDs use
lowercase ASCII letters, digits, dots, and hyphens.

## Security and compatibility

Unsigned packages are for development only. Production policy will require a
signature over the complete file with the signature field treated as zeroes.
The v0.1 packer intentionally emits unsigned development artifacts; signing
and trust-store verification are not yet implemented.

The container specification is stable only within the v0.x development series.
Do not ship production applications against it yet.
