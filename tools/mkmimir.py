#!/usr/bin/env python3
"""Write a MIMIR volume (seals, chunk and rune records of tide 0) into a disk image.

Usage: mkmimir.py [--sigil-key <ed25519.pem>] <image> <start_lba> <sector_count> <name>=<file>[@<read_perm>,<write_perm>] [...]
Format: docs/MIMIR.md and kernel/fs/mimir.c. Files ending in .sapp get write label 0x80 (system.install)
unless a label is given, the same rule the kernel's installer applies."""
import hashlib, struct, subprocess, sys, tempfile
from pathlib import Path

BLOCK, HDR = 4096, 80
PAYLOAD = BLOCK - HDR
NAME_MAX, FILE_MAX, TIDE_MAX = 48, 262144, 512
M_SEAL, M_CHUNK, M_RUNE = b"MIMR", b"MCHK", b"MRUN"
SECTORS_MIN, SECTORS_MAX = 80, 8 * (1 + 2 * TIDE_MAX)
WRITE_INSTALL = 0x80
# WARDS: default labels for new files, stored in the volume as the ordinary file "ward.rules" (docs/MIMIR.md).
DEFAULT_WARDS = (b"# MIMIR wards: <name or *suffix> <read mask> <write mask>; first match wins; applies to files created later.\n"
                 b"*.sapp 0x0 0x80\n"        # only system.install may create or replace an app
                 b"system.cfg 0x0 0x40\n"    # only settings.write may change the settings file
                 b"ward.rules 0x0 0x80\n"     # and only system.install may change the rules themselves
                 b"factory.sigil 0x0 0x80\n")


def sha(b):
    return hashlib.sha256(b).digest()


def seal(gen, tide, tail, sectors, epoch, chain):
    body = M_SEAL + struct.pack("<IIIIIII", 1, gen, tide, tail, sectors, epoch, 0) + chain
    return (body + sha(body)).ljust(512, b"\0")


def build(files, sectors):
    """files: [(name, data, read_perm, write_perm)] -> (tide0 bytes, tail blocks, chain head)."""
    tide_blocks = min((sectors // 8 - 1) // 2, TIDE_MAX)
    out, chain, seen = [], bytes(32), set()

    def append(magic, payload):
        nonlocal chain
        rid = sha(payload)
        out.append((magic + struct.pack("<I", len(payload)) + chain + rid + bytes(8) + payload).ljust(BLOCK, b"\0"))
        chain = sha(chain + rid)
        return rid

    for name, data, rp, wp in files:
        ids = []
        for off in range(0, len(data), PAYLOAD):
            piece = data[off:off + PAYLOAD]
            cid = sha(piece)
            ids.append(cid)
            if cid not in seen:
                seen.add(cid)
                append(M_CHUNK, piece)
        rune = name.encode().ljust(NAME_MAX, b"\0") + struct.pack("<IIIIII", 1, len(data), rp, wp, 0, len(ids)) + bytes(32) + b"".join(ids)
        append(M_RUNE, rune)
    if len(out) > tide_blocks:
        sys.exit("mkmimir: volume full")
    return b"".join(out), len(out), chain


def content_id(data):
    """SHA-256 of the file's chunk ids (= of its data); identical to the kernel's rec.content."""
    return sha(b"".join(sha(data[o:o + PAYLOAD]) for o in range(0, len(data), PAYLOAD)))


def sigil(files, key):
    """Signed factory manifest (docs/MIMIR.md): every file's content id and labels, Ed25519 over a domain-separated text."""
    body = b"MIMIR-SIGIL 1\n" + b"".join(
        f"{name} {content_id(data).hex()} {rp:#x} {wp:#x}\n".encode() for name, data, rp, wp in sorted(files))
    with tempfile.TemporaryDirectory() as d:
        msg, out = Path(d) / "m", Path(d) / "s"
        msg.write_bytes(b"MIMIR-SIGIL-V1\0" + body)
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey", key, "-in", str(msg), "-out", str(out)], check=True)
        return body + b"sig " + out.read_bytes().hex().encode() + b"\n"


def main():
    args = sys.argv[1:]
    key = None
    if len(args) >= 2 and args[0] == "--sigil-key":
        key, args = args[1], args[2:]
    sys.argv = [sys.argv[0]] + args
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    image, start, sectors = Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
    if not SECTORS_MIN <= sectors <= SECTORS_MAX:
        sys.exit("mkmimir: sector count out of range")
    files = []
    for spec in sys.argv[4:]:
        name, _, rest = spec.partition("=")
        path, _, labels = rest.partition("@")
        if not name or not path or len(name.encode()) >= NAME_MAX or any(ord(c) <= 32 or ord(c) > 126 for c in name):
            sys.exit(f"mkmimir: bad name in '{spec}'")
        data = Path(path).read_bytes()
        if not 0 < len(data) <= FILE_MAX:
            sys.exit(f"mkmimir: {path}: size {len(data)} outside 1..{FILE_MAX}")
        rp, wp = (int(x, 0) for x in labels.split(",")) if labels else (0, WRITE_INSTALL if name.endswith(".sapp") else 0)   # same result as the default wards
        files.append((name, data, rp, wp))
    if "ward.rules" not in {f[0] for f in files}:
        files.append(("ward.rules", DEFAULT_WARDS, 0, WRITE_INSTALL))
    if key:
        files.append(("factory.sigil", sigil(files, key), 0, WRITE_INSTALL))
    if len({f[0] for f in files}) != len(files) or len(files) > 32:
        sys.exit("mkmimir: duplicate names or too many files")
    blob, tail, chain = build(files, sectors)
    buf = bytearray(image.read_bytes())
    if (start + sectors) * 512 > len(buf):
        sys.exit("mkmimir: image too small")
    buf[start * 512:(start + sectors) * 512] = bytes(sectors * 512)
    buf[(start + 1) * 512:(start + 2) * 512] = seal(1, 0, tail, sectors, 0, chain)   # generation 1 lives in slot 1
    buf[(start + 8) * 512:(start + 8) * 512 + len(blob)] = blob
    image.write_bytes(buf)


if __name__ == "__main__":
    main()
