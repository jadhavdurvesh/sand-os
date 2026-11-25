#include "sand/fs/mimir.h"
#include "sand/storage/block.h"
#include "sand/crypto/sha256.h"
#include "sand/crypto/ed25519.h"

/* MIMIR on-disk format (see docs/MIMIR.md). All integers are little-endian.
 *  block   = 8 sectors = 4096 bytes. Block 0 of the volume holds the two seals (sector 0 = slot 0, sector 1 = slot 1).
 *  tide    = a log of blocks; tide 0 starts at block 1, tide 1 at block 1 + tide_blocks.
 *  header  = magic(4) length(4) prev-chain(32) id(32) reserved(8); payload follows (<= 4016 bytes), zero padded.
 *  chunk   = magic "MCHK", id = SHA-256(payload).      rune = magic "MRUN", id = SHA-256(payload).
 *  chain   = SHA-256(prev-chain || id), starting from 32 zero bytes at the start of a tide.
 *  seal    = "MIMR" version generation tide tail sector_count epoch 0 chain(32) SHA-256(first 64 bytes)(32). */
#define BLOCK_SECTORS 8u
#define BLOCK_BYTES 4096u
#define HDR_BYTES 80u
#define PAYLOAD_MAX (BLOCK_BYTES - HDR_BYTES)
#define MAGIC_SEAL 0x524D494Du   /* "MIMR" */
#define MAGIC_CHUNK 0x4B48434Du  /* "MCHK" */
#define MAGIC_RUNE 0x4E55524Du   /* "MRUN" */
#define TIDE_MAX 512u
#define RECS_MAX 192u
/* rune payload: name[MIMIR_NAME_MAX] then version size read_perm write_perm flags nchunks (u32 each), parent id (32), chunk ids (32 each) */
#define R_VERSION (MIMIR_NAME_MAX)
#define R_SIZE (MIMIR_NAME_MAX + 4u)
#define R_READ (MIMIR_NAME_MAX + 8u)
#define R_WRITE (MIMIR_NAME_MAX + 12u)
#define R_FLAGS (MIMIR_NAME_MAX + 16u)
#define R_NCHUNKS (MIMIR_NAME_MAX + 20u)
#define R_PARENT (MIMIR_NAME_MAX + 24u)
#define RUNE_FIXED (MIMIR_NAME_MAX + 56u)
#define CHUNKS_MAX 122u          /* (4016 - 104) / 32 chunk ids fit in one rune record */
#define SECTORS_MIN 80u
#define SECTORS_MAX (BLOCK_SECTORS * (1u + 2u * TIDE_MAX))
#define FLAG_TOMBSTONE 1u

struct rec { uint16_t block; uint8_t file; uint8_t flags; uint16_t chunks; uint16_t pad; uint32_t version, size, read_perm, write_perm; uint8_t id[32]; uint8_t content[32]; };
struct file { char name[MIMIR_NAME_MAX]; int16_t head; uint8_t used; };

static const struct sand_block_device *dev;
static uint32_t dev_index, vol_start, vol_sectors, tide_blocks;
static uint32_t generation, active, tail, epoch;
static uint8_t chain[32];
static uint8_t chunk_id[TIDE_MAX][32];
static uint16_t chunk_block[TIDE_MAX];
static uint32_t chunk_total;
static struct rec recs[RECS_MAX];
static uint32_t rec_total;
static struct file files[MIMIR_FILES];
/* Static buffers: the kernel stack is small and syscalls run with interrupts off on one CPU. */
static uint8_t out_block[BLOCK_BYTES];    /* append path */
static uint8_t in_block[BLOCK_BYTES];     /* chunk / generic reads */
static uint8_t rune_block[BLOCK_BYTES];   /* rune record being read */
static uint8_t rune_new[BLOCK_BYTES];     /* rune payload being built */
static uint8_t new_ids[CHUNKS_MAX][32];
static void policy_refresh(void);
static void policy_remount(void);

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void copy(void *d, const void *s, uint32_t n) { uint8_t *a = (uint8_t *)d; const uint8_t *b = (const uint8_t *)s; for(uint32_t i = 0; i < n; ++i) a[i] = b[i]; }
static void zero(void *d, uint32_t n) { uint8_t *a = (uint8_t *)d; for(uint32_t i = 0; i < n; ++i) a[i] = 0u; }
static int same(const uint8_t *a, const uint8_t *b, uint32_t n) { uint8_t d = 0u; for(uint32_t i = 0; i < n; ++i) d |= (uint8_t)(a[i] ^ b[i]); return d == 0u; }
static void chain_step(const uint8_t prev[32], const uint8_t id[32], uint8_t out[32]) { uint8_t both[64]; copy(both, prev, 32u); copy(both + 32, id, 32u); sand_sha256(both, 64u, out); }
static int valid_name(const char *name) {
    if(name == 0) return 0;
    uint32_t n = 0u;
    while(name[n] != '\0') { if(n >= MIMIR_NAME_MAX - 1u || (uint8_t)name[n] <= 0x20u || (uint8_t)name[n] > 0x7Eu) return 0; ++n; }
    return n > 0u;
}
static int name_eq(const char *a, const char *b) { uint32_t i = 0u; while(a[i] != '\0' && a[i] == b[i]) ++i; return a[i] == b[i]; }

static int block_rw(int write, uint32_t tide, uint32_t block, uint8_t *buf) {
    uint32_t lba = BLOCK_SECTORS * (1u + tide * tide_blocks + block);
    for(uint32_t i = 0; i < BLOCK_SECTORS; ++i) {
        int rc = write ? dev->write(vol_start + lba + i, buf + i * 512u) : dev->read(vol_start + lba + i, buf + i * 512u);
        if(rc != 0) return -1;
    }
    return 0;
}
static int chunk_find(const uint8_t *id) {
    for(uint32_t i = 0; i < chunk_total; ++i) if(same(chunk_id[i], id, 32u)) return (int)chunk_block[i];
    return -1;
}
static void state_clear(void) {
    chunk_total = 0u; rec_total = 0u; tail = 0u; zero(chain, 32u); zero(files, sizeof(files));
    for(uint32_t i = 0; i < MIMIR_FILES; ++i) files[i].head = -1;
}

/* Parse a rune payload (already hash-checked) into the in-memory version table. */
static int rune_register(uint32_t block, const uint8_t *p, uint32_t len, const uint8_t id[32]) {
    char name[MIMIR_NAME_MAX]; copy(name, p, MIMIR_NAME_MAX);
    if(len < RUNE_FIXED || name[MIMIR_NAME_MAX - 1u] != '\0' || !valid_name(name) || rec_total >= RECS_MAX) return -1;
    uint32_t version = get32(p + R_VERSION), size = get32(p + R_SIZE), rp = get32(p + R_READ), wp = get32(p + R_WRITE), flags = get32(p + R_FLAGS), nchunks = get32(p + R_NCHUNKS);
    if(nchunks > CHUNKS_MAX || len != RUNE_FIXED + 32u * nchunks || flags > FLAG_TOMBSTONE || version == 0u || size > MIMIR_FILE_MAX) return -1;
    if(nchunks != (size + PAYLOAD_MAX - 1u) / PAYLOAD_MAX) return -1;
    if((flags & FLAG_TOMBSTONE) != 0u && size != 0u) return -1;
    int slot = -1, spare = -1;
    for(uint32_t i = 0; i < MIMIR_FILES; ++i) {
        if(files[i].used) { if(name_eq(files[i].name, name)) { slot = (int)i; break; } }
        else if(spare < 0) spare = (int)i;
    }
    if(slot < 0) { if(spare < 0) return -1; slot = spare; files[slot].used = 1u; copy(files[slot].name, name, MIMIR_NAME_MAX); files[slot].head = -1; }
    if(files[slot].head >= 0 && version <= recs[files[slot].head].version) return -1;
    struct rec *r = &recs[rec_total];
    r->block = (uint16_t)block; r->file = (uint8_t)slot; r->flags = (uint8_t)flags; r->chunks = (uint16_t)nchunks; r->pad = 0u;
    r->version = version; r->size = size; r->read_perm = rp; r->write_perm = wp; copy(r->id, id, 32u);
    sand_sha256(p + RUNE_FIXED, 32u * nchunks, r->content);   /* content id: hash of the chunk ids, i.e. of the file data */
    files[slot].head = (int16_t)rec_total; ++rec_total;
    return 0;
}

/* Read and fully verify one block of `tide`; returns its magic, or 0 when it is damaged. */
static uint32_t block_check(uint32_t tide, uint32_t block, uint8_t *buf, uint32_t *len_out) {
    if(block_rw(0, tide, block, buf) != 0) return 0u;
    uint32_t magic = get32(buf), len = get32(buf + 4);
    uint8_t digest[32];
    if((magic != MAGIC_CHUNK && magic != MAGIC_RUNE) || len > PAYLOAD_MAX) return 0u;
    sand_sha256(buf + HDR_BYTES, len, digest);
    if(!same(digest, buf + 40, 32u)) return 0u;
    if(len_out) *len_out = len;
    return magic;
}

static int load_state(uint32_t tide, uint32_t blocks) {
    state_clear();
    for(uint32_t b = 0; b < blocks; ++b) {
        if(dev->read(vol_start + BLOCK_SECTORS * (1u + tide * tide_blocks + b), in_block) != 0) return -1;
        uint32_t magic = get32(in_block), len = get32(in_block + 4);
        if((magic != MAGIC_CHUNK && magic != MAGIC_RUNE) || len > PAYLOAD_MAX || !same(in_block + 8, chain, 32u)) return -1;
        uint8_t id[32]; copy(id, in_block + 40, 32u);
        if(magic == MAGIC_CHUNK) {
            if(chunk_total >= TIDE_MAX) return -1;
            if(chunk_find(id) < 0) { copy(chunk_id[chunk_total], id, 32u); chunk_block[chunk_total++] = (uint16_t)b; }
        } else {
            uint32_t got = block_check(tide, b, rune_block, 0);
            if(got != MAGIC_RUNE || get32(rune_block + 4) != len || rune_register(b, rune_block + HDR_BYTES, len, id) != 0) return -1;
        }
        uint8_t next[32]; chain_step(chain, id, next); copy(chain, next, 32u);
    }
    tail = blocks;
    return 0;
}

static void seal_bytes(uint8_t *s, uint32_t gen, uint32_t tide, uint32_t blocks, uint32_t sectors, uint32_t ep, const uint8_t head[32]) {
    zero(s, 512u);
    put32(s, MAGIC_SEAL); put32(s + 4, 1u); put32(s + 8, gen); put32(s + 12, tide); put32(s + 16, blocks); put32(s + 20, sectors); put32(s + 24, ep);
    copy(s + 32, head, 32u); sand_sha256(s, 64u, s + 64);
}
static int seal_commit(void) {
    uint8_t s[512]; seal_bytes(s, generation + 1u, active, tail, vol_sectors, epoch, chain);
    if(dev->write(vol_start + ((generation + 1u) & 1u), s) != 0) return -1;
    ++generation; return 0;
}
static uint32_t geometry(uint32_t sectors) { uint32_t t = (sectors / BLOCK_SECTORS - 1u) / 2u; return t > TIDE_MAX ? TIDE_MAX : t; }

int mimir_format(uint32_t device_index, uint32_t start_lba, uint32_t sector_count) {
    const struct sand_block_device *d = block_get(device_index);
    if(d == 0 || sector_count < SECTORS_MIN || sector_count > SECTORS_MAX || start_lba >= d->sector_count || sector_count > d->sector_count - start_lba) return -1;
    uint8_t s[512], z[32]; zero(z, 32u);
    zero(in_block, 512u);
    if(d->write(start_lba, in_block) != 0) return -1;
    seal_bytes(s, 1u, 0u, 0u, sector_count, 0u, z);
    return d->write(start_lba + 1u, s);
}
static int mount_device(void) {
    static uint8_t s[2][512]; uint32_t best_gen = 0u; int best = -1; uint8_t digest[32];
    if(dev->sector_count <= vol_start) return -1;
    for(uint32_t i = 0; i < 2u; ++i) {
        if(dev->read(vol_start + i, s[i]) != 0) continue;
        sand_sha256(s[i], 64u, digest);
        uint32_t sectors = get32(s[i] + 20), t = get32(s[i] + 12);
        if(get32(s[i]) != MAGIC_SEAL || get32(s[i] + 4) != 1u || !same(digest, s[i] + 64, 32u)) continue;
        if(sectors < SECTORS_MIN || sectors > SECTORS_MAX || sectors > dev->sector_count - vol_start || t > 1u || get32(s[i] + 16) > geometry(sectors)) continue;
        if(best < 0 || get32(s[i] + 8) > best_gen) { best = (int)i; best_gen = get32(s[i] + 8); }
    }
    if(best < 0) return -1;
    const uint8_t *b = s[best];
    vol_sectors = get32(b + 20); tide_blocks = geometry(vol_sectors); generation = best_gen; active = get32(b + 12); epoch = get32(b + 24);
    uint8_t head[32]; copy(head, b + 32, 32u);
    if(load_state(active, get32(b + 16)) != 0 || !same(chain, head, 32u)) { state_clear(); dev = 0; return -1; }
    policy_remount();
    return 0;
}
int mimir_mount(uint32_t device_index, uint32_t start_lba) {
    const struct sand_block_device *d = block_get(device_index);
    if(d == 0) return -1;
    dev = d; dev_index = device_index; vol_start = start_lba;
    return mount_device();
}
int mimir_is_mounted(void) { return dev != 0; }
static void reload(void) { if(dev != 0 && mount_device() != 0) dev = 0; }   /* drop un-sealed work after a failed update */

/* WARDS: "ward.rules" is an ordinary MIMIR file (so it has history, rollback and a hash chain); its lines give new files default labels. */
#define WARDS_MAX 8u
#define WARD_FILE "ward.rules"
struct ward { char pattern[MIMIR_NAME_MAX]; uint8_t suffix, prefix; uint32_t read_perm, write_perm; };
static struct ward wards[WARDS_MAX];
static uint32_t ward_total, ward_have;
static uint8_t ward_id[32];
static int file_find(const char *name);
static int wards_number(const uint8_t *p, const uint8_t *end, uint32_t *out) {
    uint32_t v = 0u; int digits = 0;
    if(end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        for(p += 2; p < end; ++p) { uint32_t d = *p >= '0' && *p <= '9' ? (uint32_t)(*p - '0') : *p >= 'a' && *p <= 'f' ? (uint32_t)(*p - 'a' + 10) : *p >= 'A' && *p <= 'F' ? (uint32_t)(*p - 'A' + 10) : 16u; if(d > 15u || digits >= 8) return -1; v = v * 16u + d; ++digits; }
    } else {
        for(; p < end; ++p) { if(*p < '0' || *p > '9' || digits >= 9) return -1; v = v * 10u + (uint32_t)(*p - '0'); ++digits; }
    }
    if(digits == 0) return -1;
    *out = v; return 0;
}
static void wards_parse(const uint8_t *t, uint32_t len) {
    ward_total = 0u;
    uint32_t i = 0u;
    while(i < len && ward_total < WARDS_MAX) {
        uint32_t e = i; while(e < len && t[e] != '\n') ++e;
        const uint8_t *tok[3]; const uint8_t *tend[3]; uint32_t n = 0u, p = i;
        while(p < e && n < 3u) { while(p < e && (t[p] == ' ' || t[p] == '\t' || t[p] == '\r')) ++p; if(p >= e) break; tok[n] = t + p; while(p < e && t[p] != ' ' && t[p] != '\t' && t[p] != '\r') ++p; tend[n++] = t + p; }
        if(n == 3u && tok[0][0] != '#') {
            struct ward w; uint32_t plen = (uint32_t)(tend[0] - tok[0]), skip = tok[0][0] == '*' ? 1u : 0u, tail_star = (plen > 1u && tok[0][plen - 1u] == '*' && !skip) ? 1u : 0u;
            plen -= tail_star;                                    /* a trailing star ("branch/" + star) matches everything under the branch */
            if(plen > skip && plen - skip < MIMIR_NAME_MAX && wards_number(tok[1], tend[1], &w.read_perm) == 0 && wards_number(tok[2], tend[2], &w.write_perm) == 0) {
                zero(w.pattern, MIMIR_NAME_MAX); copy(w.pattern, tok[0] + skip, plen - skip); w.suffix = (uint8_t)skip; w.prefix = (uint8_t)tail_star;
                wards[ward_total++] = w;
            }
        }
        i = e + 1u;
    }
}
/* Reload the rules when the newest version of ward.rules changed (compared by content id). Called after mount and after every commit. */
static void wards_refresh(void) {
    static uint8_t text[1025]; uint32_t len = 0u;
    int f = file_find(WARD_FILE), h = f >= 0 ? files[f].head : -1;
    if(h < 0 || (recs[h].flags & FLAG_TOMBSTONE)) { ward_total = 0u; ward_have = 0u; return; }
    if(ward_have && same(ward_id, recs[h].id, 32u)) return;
    copy(ward_id, recs[h].id, 32u); ward_have = 1u; ward_total = 0u;
    if(recs[h].size > 1024u || mimir_read(WARD_FILE, text, 1024u, &len) != 0) return;   /* unreadable or oversized rules grant nothing */
    wards_parse(text, len);
}
int mimir_ward_lookup(const char *name, uint32_t *read_perm, uint32_t *write_perm) {
    if(name == 0 || !valid_name(name)) return 0;
    uint32_t nlen = 0u; while(name[nlen] != '\0') ++nlen;
    for(uint32_t i = 0; i < ward_total; ++i) {
        uint32_t plen = 0u; while(wards[i].pattern[plen] != '\0') ++plen;
        int hit;
        if(wards[i].suffix) hit = nlen >= plen && name_eq(name + (nlen - plen), wards[i].pattern);
        else if(wards[i].prefix) { hit = nlen >= plen; for(uint32_t k = 0; hit && k < plen; ++k) if(name[k] != wards[i].pattern[k]) hit = 0; }
        else hit = name_eq(name, wards[i].pattern);
        if(hit) { if(read_perm) *read_perm = wards[i].read_perm; if(write_perm) *write_perm = wards[i].write_perm; return 1; }
    }
    return 0;
}

/* SIGIL: "factory.sigil" is a text manifest signed at build time:
 *   MIMIR-SIGIL 1\n  then one "<name> <content id hex> <read mask> <write mask>\n" per factory file,  then  "sig <128 hex>\n".
 * The signature covers "MIMIR-SIGIL-V1\0" plus every byte before the "sig" line. The content id is SHA-256 of the file's chunk ids, so it
 * survives versions, ebbs and the log chain being rebuilt; it changes when the file's data or labels change.
 * Integrity of factory files is thereby anchored in a key the kernel carries, not just in the (rewritable) hash chain. */
#define SIGIL_FILE "factory.sigil"
#define SIGIL_MAX 32u
struct sigil_entry { char name[MIMIR_NAME_MAX]; uint8_t content[32]; uint32_t read_perm, write_perm; };
static struct sigil_entry sigil_entries[SIGIL_MAX];
static uint32_t sigil_total, sigil_state, sigil_have;
static uint8_t sigil_id[32];
static int hex_bytes(const uint8_t *p, uint32_t count, uint8_t *out) {
    for(uint32_t i = 0; i < count; ++i) {
        uint32_t v = 0u;
        for(uint32_t k = 0; k < 2u; ++k) { uint8_t c = p[2u * i + k]; uint32_t d = c >= '0' && c <= '9' ? (uint32_t)(c - '0') : c >= 'a' && c <= 'f' ? (uint32_t)(c - 'a' + 10) : 16u; if(d > 15u) return -1; v = v * 16u + d; }
        out[i] = (uint8_t)v;
    }
    return 0;
}
static int sigil_parse_and_verify(const uint8_t *t, uint32_t len) {
    static const char head[] = "MIMIR-SIGIL 1\n"; static const uint8_t domain[] = "MIMIR-SIGIL-V1";
    const uint8_t *key = mimir_sigil_pubkey();
    uint32_t i = sizeof(head) - 1u; sigil_total = 0u;
    if(key == 0 || len < i || !same(t, (const uint8_t *)head, i)) return -1;
    for(;;) {
        if(i >= len) return -1;
        uint32_t e = i; while(e < len && t[e] != '\n') ++e;
        if(e >= len) return -1;
        if(e - i == 4u + 128u && t[i] == 's' && t[i + 1u] == 'i' && t[i + 2u] == 'g' && t[i + 3u] == ' ') {
            uint8_t sig[64];
            if(hex_bytes(t + i + 4u, 64u, sig) != 0 || e + 1u != len) return -1;
            return sand_ed25519_verify2(sig, key, domain, sizeof(domain), t, i) == 1 ? 0 : -1;   /* the NUL after "V1" is part of the domain */
        }
        const uint8_t *tok[4]; const uint8_t *tend[4]; uint32_t n = 0u, p = i;
        while(p < e && n < 4u) { while(p < e && t[p] == ' ') ++p; if(p >= e) break; tok[n] = t + p; while(p < e && t[p] != ' ') ++p; tend[n++] = t + p; }
        if(n != 4u || sigil_total >= SIGIL_MAX || tend[1] - tok[1] != 64) return -1;
        struct sigil_entry *s = &sigil_entries[sigil_total];
        uint32_t nl = (uint32_t)(tend[0] - tok[0]); if(nl == 0u || nl >= MIMIR_NAME_MAX) return -1;
        zero(s->name, MIMIR_NAME_MAX); copy(s->name, tok[0], nl);
        if(!valid_name(s->name) || hex_bytes(tok[1], 32u, s->content) != 0 || wards_number(tok[2], tend[2], &s->read_perm) != 0 || wards_number(tok[3], tend[3], &s->write_perm) != 0) return -1;
        ++sigil_total; i = e + 1u;
    }
}
static void sigil_refresh(void) {
    static uint8_t text[4097]; uint32_t len = 0u;
    int f = file_find(SIGIL_FILE), h = f >= 0 ? files[f].head : -1;
    if(h < 0 || (recs[h].flags & FLAG_TOMBSTONE)) { sigil_state = MIMIR_SIGIL_NONE; sigil_total = 0u; sigil_have = 0u; return; }
    if(sigil_have && same(sigil_id, recs[h].id, 32u)) return;
    copy(sigil_id, recs[h].id, 32u); sigil_have = 1u; sigil_total = 0u;
    if(mimir_sigil_pubkey() == 0) { sigil_state = MIMIR_SIGIL_NONE; return; }
    if(recs[h].size > 4096u || mimir_read(SIGIL_FILE, text, 4096u, &len) != 0 || sigil_parse_and_verify(text, len) != 0) { sigil_total = 0u; sigil_state = MIMIR_SIGIL_INVALID; return; }
    sigil_state = MIMIR_SIGIL_VALID;
}
static void policy_refresh(void) { wards_refresh(); sigil_refresh(); }
static void policy_remount(void) { ward_have = 0u; sigil_have = 0u; policy_refresh(); }   /* a (re)mount judges the policy files afresh */
static uint32_t sigil_flags(const struct rec *r) {
    if(sigil_state != MIMIR_SIGIL_VALID) return 0u;
    for(uint32_t i = 0; i < sigil_total; ++i) {
        if(!name_eq(sigil_entries[i].name, files[r->file].name)) continue;
        return same(sigil_entries[i].content, r->content, 32u) && sigil_entries[i].read_perm == r->read_perm && sigil_entries[i].write_perm == r->write_perm ? MIMIR_F_FACTORY : MIMIR_F_DRIFT;
    }
    return 0u;
}
uint32_t mimir_sigil_summary(uint32_t *listed, uint32_t *intact, uint32_t *changed, uint32_t *missing) {
    uint32_t l = 0u, a = 0u, c = 0u, m = 0u;
    if(dev != 0 && sigil_state == MIMIR_SIGIL_VALID) {
        for(uint32_t i = 0; i < sigil_total; ++i) {
            ++l; int f = file_find(sigil_entries[i].name);
            if(f < 0 || files[f].head < 0 || (recs[files[f].head].flags & FLAG_TOMBSTONE)) { ++m; continue; }
            if(sigil_flags(&recs[files[f].head]) == MIMIR_F_FACTORY) ++a; else ++c;
        }
    }
    if(listed) *listed = l;
    if(intact) *intact = a;
    if(changed) *changed = c;
    if(missing) *missing = m;
    return dev != 0 ? sigil_state : MIMIR_SIGIL_NONE;
}

static int file_find(const char *name) {
    for(uint32_t i = 0; i < MIMIR_FILES; ++i) if(files[i].used && name_eq(files[i].name, name)) return (int)i;
    return -1;
}
static int live_head(const char *name) {
    if(dev == 0 || !valid_name(name)) return -1;
    int f = file_find(name);
    if(f < 0 || files[f].head < 0 || (recs[files[f].head].flags & FLAG_TOMBSTONE)) return -1;
    return files[f].head;
}

/* Append one block to the active tide (not yet sealed). */
static int append(uint32_t magic, const uint8_t *payload, uint32_t len, const uint8_t id[32]) {
    if(tail >= tide_blocks || len > PAYLOAD_MAX) return -1;
    zero(out_block, BLOCK_BYTES);
    put32(out_block, magic); put32(out_block + 4, len); copy(out_block + 8, chain, 32u); copy(out_block + 40, id, 32u);
    copy(out_block + HDR_BYTES, payload, len);
    if(block_rw(1, active, tail, out_block) != 0) return -1;
    if(magic == MAGIC_CHUNK) { copy(chunk_id[chunk_total], id, 32u); chunk_block[chunk_total++] = (uint16_t)tail; }
    uint8_t next[32]; chain_step(chain, id, next); copy(chain, next, 32u);
    ++tail; return 0;
}
static int rune_load(const struct rec *r) {   /* into rune_block, verified */
    uint32_t len;
    if(block_check(active, r->block, rune_block, &len) != MAGIC_RUNE) return MIMIR_E_CORRUPT;
    return 0;
}
static int rune_append(const char *name, uint32_t version, uint32_t size, uint32_t rp, uint32_t wp, uint32_t flags, uint32_t nchunks, const uint8_t *ids, const uint8_t *parent) {
    uint32_t len = RUNE_FIXED + 32u * nchunks; uint8_t id[32];
    zero(rune_new, BLOCK_BYTES);
    for(uint32_t i = 0; name[i] != '\0' && i < MIMIR_NAME_MAX - 1u; ++i) rune_new[i] = (uint8_t)name[i];
    put32(rune_new + R_VERSION, version); put32(rune_new + R_SIZE, size); put32(rune_new + R_READ, rp); put32(rune_new + R_WRITE, wp); put32(rune_new + R_FLAGS, flags); put32(rune_new + R_NCHUNKS, nchunks);
    if(parent) copy(rune_new + R_PARENT, parent, 32u);
    if(nchunks) copy(rune_new + RUNE_FIXED, ids, 32u * nchunks);
    sand_sha256(rune_new, len, id);
    uint32_t block = tail;
    if(append(MAGIC_RUNE, rune_new, len, id) != 0) return -1;
    return rune_register(block, rune_new, len, id);
}

/* Copy every live file's newest version (and its chunks) into the other tide and flip the seal. */
static int ebb_run(void) {
    static uint8_t old_id[TIDE_MAX][32]; static uint16_t old_block[TIDE_MAX]; static uint16_t live[MIMIR_FILES];
    uint32_t old_total = chunk_total, old_tide = active, lives = 0u;
    for(uint32_t i = 0; i < chunk_total; ++i) { copy(old_id[i], chunk_id[i], 32u); old_block[i] = chunk_block[i]; }
    for(uint32_t f = 0; f < MIMIR_FILES; ++f) if(files[f].used && files[f].head >= 0 && !(recs[files[f].head].flags & FLAG_TOMBSTONE)) live[lives++] = recs[files[f].head].block;
    uint32_t next_epoch = epoch + 1u;
    state_clear(); active = old_tide ^ 1u;
    for(uint32_t k = 0; k < lives; ++k) {
        uint32_t len;
        if(block_check(old_tide, live[k], rune_block, &len) != MAGIC_RUNE) return -1;
        uint32_t nchunks = get32(rune_block + HDR_BYTES + R_NCHUNKS);
        static uint8_t keep[RUNE_FIXED + 32u * CHUNKS_MAX]; uint8_t rid[32]; copy(keep, rune_block + HDR_BYTES, len);
        for(uint32_t c = 0; c < nchunks; ++c) {
            const uint8_t *cid = keep + RUNE_FIXED + 32u * c;
            if(chunk_find(cid) >= 0) continue;
            int src = -1; for(uint32_t i = 0; i < old_total; ++i) if(same(old_id[i], cid, 32u)) { src = (int)old_block[i]; break; }
            uint32_t clen;
            if(src < 0 || block_check(old_tide, (uint32_t)src, in_block, &clen) != MAGIC_CHUNK) return -1;
            if(append(MAGIC_CHUNK, in_block + HDR_BYTES, clen, cid) != 0) return -1;
        }
        sand_sha256(keep, len, rid);
        uint32_t block = tail;
        if(append(MAGIC_RUNE, keep, len, rid) != 0 || rune_register(block, keep, len, rid) != 0) return -1;
    }
    epoch = next_epoch;
    return seal_commit();
}
int mimir_ebb(void) {
    if(dev == 0) return -1;
    if(ebb_run() != 0) { reload(); return -1; }
    return 0;
}

static int write_common(const char *name, const void *data, uint32_t length, int labeled, uint32_t rp, uint32_t wp) {
    if(dev == 0 || !valid_name(name) || data == 0 || length == 0u || length > MIMIR_FILE_MAX) return -1;
    uint32_t nchunks = (length + PAYLOAD_MAX - 1u) / PAYLOAD_MAX;
    const uint8_t *src = (const uint8_t *)data;
    for(uint32_t c = 0; c < nchunks; ++c) { uint32_t off = c * PAYLOAD_MAX, n = length - off > PAYLOAD_MAX ? PAYLOAD_MAX : length - off; sand_sha256(src + off, n, new_ids[c]); }
    for(int attempt = 0; attempt < 2; ++attempt) {
        int f = file_find(name), head = f >= 0 ? files[f].head : -1;
        if(!labeled) {   /* a live file keeps its labels; a new (or re-created) one takes the ward labels, if a rule matches */
            if(head >= 0 && !(recs[head].flags & FLAG_TOMBSTONE)) { rp = recs[head].read_perm; wp = recs[head].write_perm; }
            else { rp = 0u; wp = 0u; (void)mimir_ward_lookup(name, &rp, &wp); }
        }
        if(head >= 0 && !(recs[head].flags & FLAG_TOMBSTONE) && recs[head].size == length && recs[head].read_perm == rp && recs[head].write_perm == wp && recs[head].chunks == nchunks) {
            if(rune_load(&recs[head]) != 0) return MIMIR_E_CORRUPT;
            int equal = 1; for(uint32_t c = 0; c < nchunks; ++c) if(!same(rune_block + HDR_BYTES + RUNE_FIXED + 32u * c, new_ids[c], 32u)) equal = 0;
            if(equal) return 0;
        }
        uint32_t fresh = 0u;
        for(uint32_t c = 0; c < nchunks; ++c) {
            int seen = chunk_find(new_ids[c]) >= 0;
            for(uint32_t e = 0; e < c && !seen; ++e) if(same(new_ids[e], new_ids[c], 32u)) seen = 1;
            if(!seen) ++fresh;
        }
        int need_slot = f < 0; int slots = 0; for(uint32_t i = 0; i < MIMIR_FILES; ++i) if(!files[i].used) ++slots;
        if(fresh + 1u > tide_blocks - tail || rec_total >= RECS_MAX || (need_slot && slots == 0)) {
            if(attempt == 0 && ebb_run() == 0) continue;
            reload(); return -1;
        }
        for(uint32_t c = 0; c < nchunks; ++c) {
            if(chunk_find(new_ids[c]) >= 0) continue;
            uint32_t off = c * PAYLOAD_MAX, n = length - off > PAYLOAD_MAX ? PAYLOAD_MAX : length - off;
            if(append(MAGIC_CHUNK, src + off, n, new_ids[c]) != 0) { reload(); return -1; }
        }
        uint32_t version = head >= 0 ? recs[head].version + 1u : 1u;
        if(rune_append(name, version, length, rp, wp, 0u, nchunks, &new_ids[0][0], head >= 0 ? recs[head].id : 0) != 0 || seal_commit() != 0) { reload(); return -1; }
        policy_refresh();
        return 0;
    }
    return -1;
}
int mimir_write(const char *name, const void *data, uint32_t length) { return write_common(name, data, length, 0, 0u, 0u); }
int mimir_write_labeled(const char *name, const void *data, uint32_t length, uint32_t rp, uint32_t wp) { return write_common(name, data, length, 1, rp, wp); }

int mimir_read(const char *name, void *data, uint32_t capacity, uint32_t *length_out) {
    int h = live_head(name);
    if(h < 0 || data == 0 || length_out == 0) return -1;
    const struct rec *r = &recs[h];
    if(r->size > capacity) return -1;
    int rc = rune_load(r); if(rc != 0) return rc;
    uint8_t *dst = (uint8_t *)data;
    for(uint32_t c = 0; c < r->chunks; ++c) {
        int b = chunk_find(rune_block + HDR_BYTES + RUNE_FIXED + 32u * c);
        uint32_t len;
        if(b < 0) return MIMIR_E_CORRUPT;
        if(block_check(active, (uint32_t)b, in_block, &len) != MAGIC_CHUNK || !same(in_block + 40, rune_block + HDR_BYTES + RUNE_FIXED + 32u * c, 32u)) return MIMIR_E_CORRUPT;
        uint32_t off = c * PAYLOAD_MAX; if(off + len > r->size) return MIMIR_E_CORRUPT;
        copy(dst + off, in_block + HDR_BYTES, len);
    }
    *length_out = r->size; return 0;
}
int mimir_list(uint32_t ordinal, char *name_out, uint32_t name_capacity, uint32_t *length_out) {
    if(dev == 0 || name_out == 0 || name_capacity == 0u || length_out == 0) return -1;
    for(uint32_t i = 0; i < MIMIR_FILES; ++i) {
        if(!files[i].used || files[i].head < 0 || (recs[files[i].head].flags & FLAG_TOMBSTONE)) continue;
        uint32_t nl = 0u; while(files[i].name[nl] != '\0') ++nl;
        if(nl >= name_capacity) continue;           /* an old caller's small buffer cannot hold this name: skip it (FS_LIST_EX sees all) */
        if(ordinal-- != 0u) continue;
        copy(name_out, files[i].name, nl + 1u); *length_out = recs[files[i].head].size; return 0;
    }
    return -1;
}
static int new_version(int h, uint32_t flags, uint32_t rp, uint32_t wp, int copy_chunks, uint32_t from_rec) {
    const struct rec *head = &recs[h]; const struct rec *src = &recs[from_rec];
    if(rec_total >= RECS_MAX || tail >= tide_blocks) { if(ebb_run() != 0) { reload(); return -1; } return 1; }   /* 1 = collected, caller retries */
    static uint8_t ids[CHUNKS_MAX * 32u]; uint32_t n = 0u, size = 0u;
    if(copy_chunks) { if(rune_load(src) != 0) return -1; n = src->chunks; size = src->size; copy(ids, rune_block + HDR_BYTES + RUNE_FIXED, 32u * n); }
    uint8_t parent[32]; copy(parent, head->id, 32u);
    char name[MIMIR_NAME_MAX]; copy(name, files[head->file].name, MIMIR_NAME_MAX);
    if(rune_append(name, head->version + 1u, size, rp, wp, flags, n, ids, parent) != 0 || seal_commit() != 0) { reload(); return -1; }
    policy_refresh();
    return 0;
}
static int mutate(const char *name, int kind, uint32_t a, uint32_t b) {
    for(int attempt = 0; attempt < 2; ++attempt) {
        int h = live_head(name); if(h < 0) return -1;
        const struct rec *hr = &recs[h]; int from = h; uint32_t flags = 0u, rp = hr->read_perm, wp = hr->write_perm; int chunks = 1;
        if(kind == 0) { flags = FLAG_TOMBSTONE; chunks = 0; }
        else if(kind == 1) {   /* rollback to version a, current labels */
            int found = -1; for(int i = (int)rec_total - 1; i >= 0; --i) if(recs[i].file == hr->file && recs[i].version == a && !(recs[i].flags & FLAG_TOMBSTONE)) { found = i; break; }
            if(found < 0) return -1;
            from = found;
        } else { rp = a; wp = b; }
        int rc = new_version(h, flags, rp, wp, chunks, (uint32_t)from);
        if(rc <= 0) return rc;
    }
    return -1;
}
int mimir_remove(const char *name) { return mutate(name, 0, 0u, 0u); }

/* HEL: removal only appends a tombstone, so the last live version is still there until an ebb collects it. */
static int trash_head(int f) {                      /* newest live version of a file whose head is a tombstone, or -1 */
    if(f < 0 || files[f].head < 0 || !(recs[files[f].head].flags & FLAG_TOMBSTONE)) return -1;
    for(int i = (int)rec_total - 1; i >= 0; --i) if(recs[i].file == (uint8_t)f && !(recs[i].flags & FLAG_TOMBSTONE)) return i;
    return -1;
}
int mimir_trash_list(uint32_t ordinal, char *name_out, uint32_t name_capacity, struct mimir_info *out) {
    if(dev == 0 || name_out == 0 || name_capacity == 0u || out == 0) return -1;
    for(uint32_t f = 0; f < MIMIR_FILES; ++f) {
        int h = files[f].used ? trash_head((int)f) : -1; if(h < 0) continue;
        uint32_t nl = 0u; while(files[f].name[nl] != '\0') ++nl;
        if(nl >= name_capacity) continue;
        if(ordinal-- != 0u) continue;
        copy(name_out, files[f].name, nl + 1u);
        out->size = recs[h].size; out->version = recs[h].version; out->read_perm = recs[h].read_perm; out->write_perm = recs[h].write_perm; out->chunks = recs[h].chunks; out->flags = 0u; copy(out->id, recs[h].id, 32u);
        return 0;
    }
    return -1;
}
int mimir_restore(const char *name) {               /* appends a new version with the content and labels of the last live one */
    for(int attempt = 0; attempt < 2; ++attempt) {
        if(dev == 0 || !valid_name(name)) return -1;
        int f = file_find(name), src = trash_head(f); if(src < 0) return -1;
        int rc = new_version(files[f].head, 0u, recs[src].read_perm, recs[src].write_perm, 1, (uint32_t)src);
        if(rc <= 0) return rc;                       /* 1 = an ebb ran to make room: the trash is gone with it, so the retry fails */
    }
    return -1;
}
int mimir_rollback(const char *name, uint32_t version) { return mutate(name, 1, version, 0u); }
int mimir_set_labels(const char *name, uint32_t read_perm, uint32_t write_perm) { return mutate(name, 2, read_perm, write_perm); }

int mimir_stat(const char *name, struct mimir_info *out) {
    int h = live_head(name); if(h < 0 || out == 0) return -1;
    const struct rec *r = &recs[h];
    out->size = r->size; out->version = r->version; out->read_perm = r->read_perm; out->write_perm = r->write_perm; out->chunks = r->chunks; out->flags = r->flags | sigil_flags(r); copy(out->id, r->id, 32u);
    return 0;
}
int mimir_history(const char *name, uint32_t index, struct mimir_ver *out) {
    if(dev == 0 || !valid_name(name) || out == 0) return -1;
    int f = file_find(name); if(f < 0) return -1;
    for(int i = (int)rec_total - 1; i >= 0; --i) {
        if(recs[i].file != (uint8_t)f) continue;
        if(index-- != 0u) continue;
        out->version = recs[i].version; out->size = recs[i].size; out->flags = recs[i].flags; out->reserved = 0u; copy(out->id, recs[i].id, 32u);
        return 0;
    }
    return -1;
}
int mimir_seal_info(struct mimir_seal *out) {
    if(dev == 0 || out == 0) return -1;
    out->generation = generation; out->tide = active; out->epoch = epoch; out->used_blocks = tail; out->tide_blocks = tide_blocks; out->sigil = dev != 0 ? sigil_state : 0u;
    out->files = 0u; for(uint32_t i = 0; i < MIMIR_FILES; ++i) if(files[i].used && files[i].head >= 0 && !(recs[files[i].head].flags & FLAG_TOMBSTONE)) ++out->files;
    out->versions = rec_total; copy(out->chain, chain, 32u);
    return 0;
}
/* Full scrub: re-read every block of the active tide, recheck each hash and the whole chain against the seal. */
int mimir_scrub(struct mimir_scrub *out) {
    if(dev == 0 || out == 0) return -1;
    uint8_t run[32], next[32]; zero(run, 32u); out->blocks = 0u; out->bad = 0u; out->first_bad = 0u; out->reserved = 0u;
    for(uint32_t b = 0; b < tail; ++b) {
        uint32_t len; int bad = block_check(active, b, in_block, &len) == 0u || !same(in_block + 8, run, 32u);
        ++out->blocks;
        if(bad) { if(out->bad++ == 0u) out->first_bad = b; }
        chain_step(run, in_block + 40, next); copy(run, next, 32u);
    }
    if(!same(run, chain, 32u) && out->bad == 0u) { out->bad = 1u; out->first_bad = tail; }
    return 0;
}
