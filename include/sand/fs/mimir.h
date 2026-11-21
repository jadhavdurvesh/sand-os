#ifndef SAND_FS_MIMIR_H
#define SAND_FS_MIMIR_H
#include <stdint.h>
/*
 * MIMIR - SAND's own filesystem. No inodes, no allocation bitmap, no directory blocks.
 *
 *  - Content-addressed: a file is a list of 4016-byte chunks named by their SHA-256; equal chunks are stored once.
 *  - Versioned: every write appends a new immutable "rune" record that points to its parent; nothing is overwritten.
 *  - Self-verifying: every block carries its own hash and the log is a hash chain whose head is signed into a "seal".
 *    Reads recheck the hash, so a flipped bit is reported (MIMIR_E_CORRUPT), never returned as data.
 *  - Crash-safe: new blocks are only believed once an alternating pair of seals (A/B, generation counter) names them.
 *  - Twin tides: the volume has two log areas; when the active tide fills, the "ebb" copies the live files into the
 *    other tide and flips the seal (a copying collector; history older than the ebb is dropped).
 *  - Veyra labels: every file version carries a read mask and a write mask of SAND_PERM_* bits that a caller must hold.
 * Layout and byte formats: docs/MIMIR.md. tools/mkmimir.py writes the same format.
 */
#define MIMIR_NAME_MAX 24u
#define MIMIR_FILE_MAX 65536u
#define MIMIR_FILES 32u
#define MIMIR_E_CORRUPT (-2)

struct mimir_info { uint32_t size, version, read_perm, write_perm, chunks, flags; uint8_t id[32]; };
struct mimir_ver  { uint32_t version, size, flags, reserved; uint8_t id[32]; };
struct mimir_seal { uint32_t generation, tide, epoch, used_blocks, tide_blocks, files, versions, reserved; uint8_t chain[32]; };
struct mimir_scrub { uint32_t blocks, bad, first_bad, reserved; };

int mimir_format(uint32_t device_index, uint32_t start_lba, uint32_t sector_count);
int mimir_mount(uint32_t device_index, uint32_t start_lba);
int mimir_is_mounted(void);
/* Write a new version, keeping the labels of the previous one (new files get none). Identical content is a no-op. */
int mimir_write(const char *name, const void *data, uint32_t length);
int mimir_write_labeled(const char *name, const void *data, uint32_t length, uint32_t read_perm, uint32_t write_perm);
/* 0, -1 (missing, too big, bad argument), or MIMIR_E_CORRUPT when a stored hash no longer matches. */
int mimir_read(const char *name, void *data, uint32_t capacity, uint32_t *length_out);
int mimir_list(uint32_t ordinal, char *name_out, uint32_t name_capacity, uint32_t *length_out);
int mimir_remove(const char *name);
int mimir_stat(const char *name, struct mimir_info *out);
int mimir_history(const char *name, uint32_t index_from_newest, struct mimir_ver *out);
int mimir_rollback(const char *name, uint32_t version);
int mimir_set_labels(const char *name, uint32_t read_perm, uint32_t write_perm);
int mimir_seal_info(struct mimir_seal *out);
int mimir_scrub(struct mimir_scrub *out);
int mimir_ebb(void);
#endif
