#!/usr/bin/env python3
"""Copy the Citadel core + SandOS adapter from a Citadel checkout into third_party/citadel/.

Usage: tools/sync_citadel.py <path-to-citadel-checkout>

Only the files the SAPP needs are copied (no host adapters, catalog or provider ABI), and the source commit is
recorded in third_party/citadel/VERSION so the vendored copy can be traced and re-synced."""
import shutil, subprocess, sys
from pathlib import Path

FILES = ["include/citadel/citadel.h", "include/citadel/sandos.h", "src/core/parser.c", "src/core/registry.c",
         "src/core/dispatcher.c", "src/adapters/sandos.c", "LICENSE", "NOTICE"]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    src, dst = Path(sys.argv[1]), Path(__file__).resolve().parent.parent / "third_party" / "citadel"
    commit = subprocess.run(["git", "-C", str(src), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", str(src), "status", "--porcelain"], capture_output=True, text=True, check=True).stdout.strip()
    if dirty:
        sys.exit("sync_citadel: the Citadel checkout has uncommitted changes; commit or stash them first")
    if dst.exists():
        shutil.rmtree(dst)
    for rel in FILES:
        (dst / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(src / rel, dst / rel)
    (dst / "VERSION").write_text(f"Citadel {commit}\nSynced by tools/sync_citadel.py; do not edit these files here, change them in the Citadel repository.\n")
    print(f"vendored Citadel {commit[:12]} into {dst}")


if __name__ == "__main__":
    main()
