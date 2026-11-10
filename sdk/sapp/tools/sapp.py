#!/usr/bin/env python3
"""SAPP v0.1 development packer and validator."""
import argparse
import json
import pathlib
import struct
import sys

MAGIC = b"SAPP"
VERSION = 1
ARCH_X86_32 = 1
HEADER_SIZE = 64
SEGMENT_SIZE = 32
PERMS = {"r": 1, "w": 2, "x": 4}
HEADER = struct.Struct("<4sHHIIIIIIIIII16s")
SEGMENT = struct.Struct("<IIIIII8s")


def fail(message):
    raise ValueError(message)


def read_manifest(path):
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    for key in ("id", "name", "version"):
        if not isinstance(data.get(key), str) or not data[key].strip():
            fail(f"manifest requires non-empty string '{key}'")
    app_id = data["id"]
    allowed = set("abcdefghijklmnopqrstuvwxyz0123456789.-")
    if any(ch not in allowed for ch in app_id) or app_id[0] in ".-" or app_id[-1] in ".-":
        fail("manifest id must use lowercase ASCII letters, digits, dots and hyphens")
    return data


def pack(args):
    image = pathlib.Path(args.input).read_bytes()
    if not image:
        fail("input image is empty")
    if len(image) > 64 * 1024 * 1024:
        fail("input image exceeds 64 MiB development limit")
    manifest = read_manifest(args.manifest)
    base, entry = int(args.base, 0), int(args.entry, 0)
    if base < 0 or base > 0xffffffff or entry < base or entry >= base + len(image):
        fail("entry must lie inside the input image address range")
    perm = 0
    for ch in args.permissions:
        if ch not in PERMS:
            fail(f"unknown permission '{ch}'")
        perm |= PERMS[ch]
    if not (perm & PERMS["x"]) or (perm & (PERMS["w"] | PERMS["x"])) == (PERMS["w"] | PERMS["x"]):
        fail("v1 requires executable, non-W+X segment permissions")
    table_off = HEADER_SIZE
    metadata = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode("utf-8")
    metadata_off = table_off + SEGMENT_SIZE
    payload_off = (metadata_off + len(metadata) + 15) & ~15
    file_size = payload_off + len(image)
    header = HEADER.pack(MAGIC, VERSION, ARCH_X86_32, HEADER_SIZE, 0, entry,
                         table_off, 1, metadata_off, len(metadata), file_size,
                         0, 0, bytes(16))
    segment = SEGMENT.pack(base, len(image), payload_off, len(image), perm,
                           4096, bytes(8))
    output = header + segment + metadata
    output += bytes(payload_off - len(output)) + image
    pathlib.Path(args.output).write_bytes(output)
    print(f"created {args.output}: {file_size} bytes ({len(image)} byte image)")


def parse(path):
    blob = pathlib.Path(path).read_bytes()
    if len(blob) < HEADER_SIZE:
        fail("file is shorter than the SAPP header")
    fields = HEADER.unpack_from(blob)
    (magic, version, arch, hsize, flags, entry, table_off, count,
     meta_off, meta_len, file_size, sig_off, sig_len, reserved) = fields
    if magic != MAGIC:
        fail("bad SAPP magic")
    if version != VERSION or arch != ARCH_X86_32:
        fail("unsupported SAPP version or architecture")
    if hsize != HEADER_SIZE or flags != 0 or reserved != bytes(16):
        fail("invalid header size, flags or reserved bytes")
    if file_size != len(blob):
        fail("declared file size does not match actual file size")
    if count == 0 or count > 64:
        fail("invalid segment count")
    table_end = table_off + count * SEGMENT_SIZE
    if table_off < HEADER_SIZE or table_end > len(blob):
        fail("segment table is out of bounds")
    if meta_off < table_end or meta_off + meta_len > len(blob):
        fail("metadata range is out of bounds")
    if sig_off or sig_len:
        fail("signed SAPP files are not supported by this development validator yet")
    metadata = json.loads(blob[meta_off:meta_off + meta_len].decode("utf-8"))
    segments = []
    virtual_ranges = []
    file_ranges = []
    executable_entry = False
    for i in range(count):
        seg = SEGMENT.unpack_from(blob, table_off + i * SEGMENT_SIZE)
        vaddr, memsz, off, filesz, perms, align, zero = seg
        if zero != bytes(8) or memsz == 0 or filesz > memsz:
            fail(f"segment {i}: invalid sizes or reserved bytes")
        if perms & ~7 or not (perms & PERMS["r"]) or (perms & 6) == 6:
            fail(f"segment {i}: invalid permissions (W+X forbidden)")
        if align < 4096 or align & (align - 1) or vaddr % align:
            fail(f"segment {i}: invalid alignment")
        if off < meta_off + meta_len or off + filesz > len(blob):
            fail(f"segment {i}: payload out of bounds or overlaps metadata")
        if vaddr + memsz > 0x100000000:
            fail(f"segment {i}: virtual address overflow")
        virtual_ranges.append((vaddr, vaddr + memsz))
        file_ranges.append((off, off + filesz))
        if perms & PERMS["x"] and vaddr <= entry < vaddr + memsz:
            executable_entry = True
        segments.append({"vaddr": hex(vaddr), "memsz": memsz, "file_offset": off,
                         "filesz": filesz, "permissions": perms, "alignment": align})
    for ranges, label in ((virtual_ranges, "virtual"), (file_ranges, "file")):
        ordered = sorted(ranges)
        for left, right in zip(ordered, ordered[1:]):
            if left[1] > right[0]:
                fail(f"overlapping {label} segments")
    if not executable_entry:
        fail("entry point is not inside an executable segment")
    if not isinstance(metadata, dict) or not all(isinstance(metadata.get(k), str) and metadata[k] for k in ("id", "name", "version")):
        fail("metadata is missing required id/name/version")
    return {"version": version, "architecture": "x86-32", "entry": hex(entry),
            "file_size": file_size, "metadata": metadata, "segments": segments}


def main():
    parser = argparse.ArgumentParser(prog="sapp", description="SAND native application format tools")
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("pack", help="package a flat i386 image as a SAPP executable")
    p.add_argument("--input", required=True)
    p.add_argument("--manifest", required=True)
    p.add_argument("--output", required=True)
    p.add_argument("--base", default="0x40000000")
    p.add_argument("--entry", default="0x40000000")
    p.add_argument("--permissions", default="rx")
    p.set_defaults(func=pack)
    for name in ("verify", "info"):
        p = sub.add_parser(name, help=f"{name} a SAPP file")
        p.add_argument("file")
        p.set_defaults(func=lambda a, n=name: print(json.dumps(parse(a.file), indent=2)))
    args = parser.parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, json.JSONDecodeError, struct.error) as exc:
        print(f"sapp: error: {exc}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
