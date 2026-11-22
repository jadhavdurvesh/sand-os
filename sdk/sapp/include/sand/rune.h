#ifndef SAND_RUNE_H
#define SAND_RUNE_H
/* RUNELIB: app-facing helpers over the MIMIR syscalls (ABI v13). Header-only; needs sand/libsand.h.
 * Every call is still checked by the kernel: fs.read / fs.write for the data, a file's own read/write labels
 * (Veyra permission masks), and the wards for new files. Nothing here bypasses those checks. */
#include "sand/libsand.h"

#define SAND_RUNE_NAME_MAX 47u

/* Store a file (new version unless the content is identical: MIMIR dedupes chunks and the id then matches). */
static inline int32_t sand_rune_put(const char *name, const void *data, uint32_t len) { return sand_fs_write(name, data, len); }
static inline int32_t sand_rune_put_text(const char *name, const char *text) { return sand_fs_write(name, text, strlen(text)); }
static inline int32_t sand_rune_get(const char *name, void *buf, uint32_t cap) { return sand_fs_read(name, buf, cap); }

/* Number of the newest version (1 for a file written once), or a negative error. */
static inline int32_t sand_rune_version(const char *name) {
    struct sand_fsstat st;
    int32_t r = sand_fs_stat(name, &st);
    return r < 0 ? r : (int32_t)st.version;
}
/* 1 when the two files hold byte-identical content, 0 when not, negative error otherwise. Rune ids include the name
 * and version, so content is compared directly: `scratch` must hold both files (2 * size); else SAND_E_FAIL. */
static inline int32_t sand_rune_same(const char *a, const char *b, void *scratch, uint32_t cap) {
    struct sand_fsstat x, y;
    int32_t r = sand_fs_stat(a, &x); if (r < 0) return r;
    r = sand_fs_stat(b, &y); if (r < 0) return r;
    if (x.size != y.size) return 0;
    if (cap < 2u * x.size) return SAND_E_FAIL;
    uint8_t *p = (uint8_t *)scratch;
    if (sand_fs_read(a, p, x.size) != (int32_t)x.size) return SAND_E_FAIL;
    if (sand_fs_read(b, p + x.size, y.size) != (int32_t)y.size) return SAND_E_FAIL;
    return memcmp(p, p + x.size, x.size) == 0;
}
/* Roll back to the version before the newest live one (history is appended, never rewritten).
 * Returns the version number restored, or a negative error (SAND_E_NOENT when there is nothing earlier). */
static inline int32_t sand_rune_undo(const char *name) {
    struct sand_fsstat st; struct sand_fsver v;
    int32_t r = sand_fs_stat(name, &st); if (r < 0) return r;
    for (uint32_t i = 1; sand_fs_history(name, i, &v) == 0; ++i) {
        if ((v.flags & 1u) != 0u) continue;                                      /* skip versions that deleted the file */
        r = sand_fs_rollback(name, v.version);
        return r < 0 ? r : (int32_t)v.version;
    }
    return SAND_E_NOENT;
}
/* Restrict a file to callers holding these SAND_PERM_* masks. Needs system.install (the kernel decides). */
static inline int32_t sand_rune_protect(const char *name, uint32_t read_mask, uint32_t write_mask) { return sand_fs_label(name, read_mask, write_mask); }

/* Branch listing: calls fn(name, length, is_branch, user) for each direct child of `branch` ("" = top level,
 * "notes/" = inside notes/). Deeper levels collapse into one is_branch entry, like the shell's ls.
 * Returns the number of entries delivered. */
typedef void (*sand_rune_visit)(const char *name, uint32_t length, int is_branch, void *user);
static inline int32_t sand_rune_walk(const char *branch, sand_rune_visit fn, void *user) {
    struct sand_dirent_ex e; char seen[16][SAND_RUNE_NAME_MAX + 1]; uint32_t nseen = 0, blen = strlen(branch); int32_t count = 0;
    for (uint32_t i = 0; sand_fs_list_ex(i, &e) == 0; ++i) {
        if (blen != 0u && strncmp(e.name, branch, blen) != 0) continue;
        if (strlen(e.name) <= blen) continue;
        const char *rest = e.name + blen; uint32_t k = 0;
        while (rest[k] != 0 && rest[k] != '/') ++k;
        if (rest[k] == '/') {                                                    /* a deeper branch: report it once */
            char head[SAND_RUNE_NAME_MAX + 2]; uint32_t n = blen + k + 1u, dup = 0;
            memcpy(head, e.name, n); head[n] = 0;
            for (uint32_t s = 0; s < nseen; ++s) if (strcmp(seen[s], head) == 0) dup = 1;
            if (dup) continue;
            if (nseen < 16u) memcpy(seen[nseen++], head, n + 1u);
            fn(head, 0u, 1, user); ++count;
        } else { fn(e.name, e.length, 0, user); ++count; }
    }
    return count;
}

/* Integrity check: 1 when the whole store re-hashes clean and the chain matches its seal, 0 when damage was found,
 * negative error otherwise. */
static inline int32_t sand_rune_healthy(void) {
    struct sand_fsscrub s; int32_t r = sand_fs_scrub(&s);
    return r < 0 ? r : (s.bad == 0u);
}
#endif
