# MIMIR - SAND's own filesystem

MIMIR (the Norse well of memory) is SAND's native storage format. It replaces the earlier SANDFS prototype. It is built around ideas that
differ from the FAT / ext / NTFS / APFS family: there are **no inodes, no allocation tables, no directory blocks and no in-place updates**.

| Idea | What it means in MIMIR |
|---|---|
| **Content-addressed chunks** | A file is a list of 4016-byte chunks named by their SHA-256. Equal chunks are stored once (verified: a copy of a 60 000-byte file costs one block). |
| **Runes, not inodes** | A *rune* is an immutable record: name, version, size, Veyra labels, parent hash, chunk list. A write appends a new rune; the old one stays as history. |
| **Everything is self-verifying** | Every block carries its own hash; the log is a hash chain whose head is stored in the seal. A flipped bit is reported as `SAND_E_CORRUPT`, it is never returned as file data. |
| **Atomic by construction** | New blocks are only believed once a *seal* names them. Two seal slots alternate with a generation counter; a torn seal falls back one generation. No journal, no fsck. |
| **Twin tides** | The volume holds two log areas. When the active one fills, the *ebb* copies the live files into the other tide and flips the seal (a copying collector). Content addressing means rune ids survive the move. |
| **Wards** | The policy lives in the store: the ordinary file `ward.rules` (`<name or *suffix> <read mask> <write mask>`, first match wins, at most 8 rules, 1 KiB) gives *new* files their default labels and decides who may create them. It is versioned, hash-chained and roll-back-able like any file (`mimir log ward.rules`, `mimir undo ward.rules 1`). The image ships `*.sapp -> write system.install`, `system.cfg -> write settings.write` and `ward.rules -> write system.install`. |
| **Sigil** | The build signs a *factory manifest* (`factory.sigil`: every factory file's content id and labels) with Ed25519; the kernel carries the public key and checks the signature at mount. The content id is the SHA-256 of the file's chunk ids, so it survives versions, rollbacks and ebbs and changes only when data or labels change. `mimir stat` / `mimir sigil` then show each file as *sealed* (exactly as shipped), *CHANGED* (listed but modified since) or a user file; an edited or forged manifest makes the whole sigil INVALID and nothing is vouched for. Boot prints `SAND_TEST:M6_SIGIL_VALID`. |
| **Veyra labels** | Every file version carries a read mask and a write mask of `SAND_PERM_*` bits. The syscall layer refuses callers that do not hold them, and audits it (`MIMIR_LABEL_DENIED`). |

## Layout (little-endian)

```
volume (start LBA S, N sectors, 80 <= N <= 8200; the image uses 8192 = 4 MiB at LBA 2112)
  block 0 (sectors 0-7): sector 0 = seal slot 0, sector 1 = seal slot 1, rest reserved
  tide 0: blocks 1 .. T            T = min((N/8 - 1)/2, 512)
  tide 1: blocks T+1 .. 2T
block  = 8 sectors = 4096 bytes
header = magic(4) length(4) prev-chain(32) id(32) reserved(8)      payload follows, <= 4016 bytes, zero padded
chunk  : magic "MCHK", id = SHA-256(payload)
rune   : magic "MRUN", id = SHA-256(payload)
         payload = name[24] version(4) size(4) read_perm(4) write_perm(4) flags(4) nchunks(4) parent(32) chunk-id(32) x nchunks
chain  : chain_0 = 32 zero bytes at the start of a tide;  chain_i = SHA-256(chain_(i-1) || id_i);  header.prev = chain_(i-1)
seal   : "MIMR" version(=1) generation tide tail sector_count epoch 0 chain(32) SHA-256(first 64 bytes)(32)   (one sector)
```

Mount reads both seals, takes the valid one with the highest generation, then walks `tail` blocks of that tide: chunk headers are chained
without reading payloads, rune records are read and hash-checked in full and rebuild the in-memory name table. The final chain must equal the
seal's. Chunk payloads are verified lazily on every read, and `mimir verify` (syscall `FS_SCRUB`) re-reads and re-hashes everything.

## Operations

- **write**: hash the chunks, skip those already stored, append the new chunks, append a rune (version + 1, parent = previous rune id), write the *other* seal slot. Rewriting identical content with identical labels is a no-op. A new version inherits the previous labels.
- **rollback** (`mimir undo f v`): appends a new rune carrying version v's content. History is never rewritten. Labels stay as they are now.
- **remove**: appends a tombstone rune (version continues if the name is written again).
- **ebb** (automatic when a tide is full, or `mimir ebb`): live files only; removed files and old versions are dropped. A failed or interrupted ebb leaves the old seal in force.
- **wards**: a file that does not exist (or was removed) is created with the labels of the first matching rule, and the syscall layer checks the creator holds that rule's write mask, so an app with only `fs.write` can no longer drop a `.sapp` file on disk. Live files keep their own labels. Changing `ward.rules` takes effect immediately for files created afterwards (existing files are not relabelled; use `mimir label`). A missing, oversized or unreadable rules file means no rules (fail open: the permission gate and labels already set still apply), malformed lines are skipped.
- **labels**: `FS_LABEL` needs `system.install`. The kernel writes `*.sapp` with write label `0x80` (`system.install`) and `system.cfg` with write label `0x40` (`settings.write`).

## Sigil format

```
factory.sigil (text, <= 4096 bytes, write label system.install):
MIMIR-SIGIL 1\n
<name> <content id: 64 hex> <read mask> <write mask>\n     (one per factory file, sorted by name, at most 32)
sig <128 hex Ed25519 signature>\n
signature = Ed25519( "MIMIR-SIGIL-V1\0" || every byte before the "sig" line )
```
`tools/mkmimir.py --sigil-key key.pem` writes and signs it (OpenSSL 3 Ed25519); the Makefile generates a throw-away developer key under `build/sigil/` and links its public half into the kernel (`kernel/fs/sigil_key.c`), like the STCI test material. `tests/fs/mimir_test.c` verifies an OpenSSL signature with the kernel's own Ed25519 code and exercises tamper, rollback, ebb and forged-manifest cases.

## Interfaces

Kernel API `include/sand/fs/mimir.h`; syscalls 8-11 (read/write/list/remove, now label-checked) and, since ABI v12, 42-48:
`FS_STAT`, `FS_HISTORY`, `FS_ROLLBACK`, `FS_SEAL`, `FS_SCRUB`, `FS_LABEL`, `FS_EBB` (`SAND_E_CORRUPT` = -10). Veyra interface `0x0106` STORE
(and `0x0101` FILES keeps working). Shell: `mimir [seal | sigil | stat f | log f | undo f v | verify | ebb | label f r w]`.
`tools/mkmimir.py` builds the same format at image build time; `tests/fs/mimir_test.c` mounts a Python-built volume to prove both agree.

## Limits (honest)

- 32 files, 192 versions per tide, names up to 23 printable ASCII characters, files up to 256 KiB (a rune record could address ~490 KB; three kernel buffers of that size are allocated once and never freed, which is the limit). One flat namespace, no directories, no file handles, no partial reads or writes.
- History lives only inside the active tide: an ebb drops it. The volume is 4 MiB (2 tides of 511 blocks); live data must fit in one tide.
- The hash chain proves integrity, not authenticity: anyone who can write the disk can rebuild a consistent chain. The sigil adds authenticity only for the *factory* files: it cannot vouch for files created later (nothing at runtime holds a signing key), and the key in the repository build is a throw-away developer key, so it proves nothing about a real release until a provisioned key is embedded. An attacker who can replace both the kernel and the disk can replace the key too (no secure boot / measured boot here).
- Wards are matched by file name only (exact or `*suffix`), not by content or location; there are no directories to attach them to.
- Labels are enforced in the syscall layer; kernel code (desktop settings, installer) writes directly. There is no encryption.
- Single CPU, interrupts off, polling ATA PIO; every sector write is synchronous, so writes are slow (a 256 KiB file is 66 blocks of 8 sector writes). A failed update reloads state from disk.
- A mount failure on a disk that is not MIMIR formats the volume (as the earlier boot code did). Existing SANDFS volumes are not converted: the image builder always writes MIMIR.
- Evidence is host tests (RAM disk with fault injection, ASan/UBSan) and QEMU; no real hardware.
