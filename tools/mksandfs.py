#!/usr/bin/env python3
"""Write a SANDFS volume (superblock, directory, contiguous file extents) into a disk image.

Usage: mksandfs.py <image> <start_lba> <sector_count> <name>=<file> [...]
Layout matches kernel/fs/sandfs.c: sector 0 superblock, sector 1 directory (16 entries of
name[24] + absolute data LBA + length), data from sector 3; each file is a run of whole sectors."""
import struct, sys
from pathlib import Path

NAME_MAX, ENTRIES, FILE_MAX, MAGIC, VERSION = 24, 16, 65536, 0x53414E44, 1


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    image, start, count = Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    files = []
    for spec in sys.argv[4:]:
        name, _, path = spec.partition("=")
        data = Path(path).read_bytes()
        if not name or len(name.encode()) >= NAME_MAX or not path:
            sys.exit(f"mksandfs: bad name in '{spec}'")
        if not 0 < len(data) <= FILE_MAX:
            sys.exit(f"mksandfs: {path}: size {len(data)} outside 1..{FILE_MAX}")
        files.append((name, data))
    if len(files) > ENTRIES or len({n for n, _ in files}) != len(files):
        sys.exit("mksandfs: too many or duplicate files")
    buf = bytearray(image.read_bytes())
    next_lba, entries = 3, bytearray()
    for name, data in files:
        sectors = (len(data) + 511) // 512
        if next_lba + sectors > count or (start + next_lba + sectors) * 512 > len(buf):
            sys.exit("mksandfs: volume full")
        buf[(start + next_lba) * 512:(start + next_lba) * 512 + len(data)] = data
        entries += struct.pack("<24sII", name.encode(), start + next_lba, len(data))
        next_lba += sectors
    super_ = struct.pack("<6I", MAGIC, VERSION, count, next_lba, 1, 3) + bytes(488)
    buf[start * 512:(start + 1) * 512] = super_
    buf[(start + 1) * 512:(start + 2) * 512] = entries.ljust(512, b"\0")
    image.write_bytes(buf)


if __name__ == "__main__":
    main()
