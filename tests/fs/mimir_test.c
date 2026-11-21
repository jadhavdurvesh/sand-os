/* Host tests for MIMIR (kernel/fs/mimir.c) on a RAM disk: versions, dedupe, labels, ebb, corruption, torn writes.
 * With arguments it also mounts a volume built by tools/mkmimir.py:  mimir-test <image> <start> <sectors> name=file ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sand/fs/mimir.h"
#include "sand/storage/block.h"

#define SECTORS 8192u
static uint8_t *ram; static uint32_t ram_sectors; static long writes_left = -1; static uint32_t base_lba;
static int ram_read(uint32_t lba, uint8_t *b) { if(lba >= ram_sectors) return -1; memcpy(b, ram + (size_t)lba * 512u, 512u); return 0; }
static int ram_write(uint32_t lba, const uint8_t *b) { if(lba >= ram_sectors || writes_left == 0) return -1; if(writes_left > 0) --writes_left; memcpy(ram + (size_t)lba * 512u, b, 512u); return 0; }
static const struct sand_block_device disk = { "ram", 0, ram_read, ram_write };
static struct sand_block_device mutable_disk;
static int failures;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++failures; } } while(0)

static void fresh(void) { memset(ram, 0, (size_t)ram_sectors * 512u); writes_left = -1; CHECK(mimir_format(0, base_lba, SECTORS) == 0); CHECK(mimir_mount(0, base_lba) == 0); }
static void fill(uint8_t *p, uint32_t n, uint32_t seed) { for(uint32_t i = 0; i < n; ++i) { seed = seed * 1664525u + 1013904223u; p[i] = (uint8_t)(seed >> 24); } }
static uint32_t used(void) { struct mimir_seal s; CHECK(mimir_seal_info(&s) == 0); return s.used_blocks; }
static int read_eq(const char *name, const uint8_t *want, uint32_t n) { static uint8_t buf[MIMIR_FILE_MAX]; uint32_t len = 0; int rc = mimir_read(name, buf, sizeof(buf), &len); return rc == 0 && len == n && memcmp(buf, want, n) == 0; }

static void test_basic(void) {
    static uint8_t a[60000], big[MIMIR_FILE_MAX]; fill(a, sizeof(a), 1); fill(big, sizeof(big), 2);
    fresh();
    CHECK(mimir_write("hello.txt", "hi", 2) == 0); CHECK(read_eq("hello.txt", (const uint8_t *)"hi", 2));
    CHECK(mimir_write("a.bin", a, sizeof(a)) == 0); CHECK(read_eq("a.bin", a, sizeof(a)));
    CHECK(mimir_write("big.bin", big, sizeof(big)) == 0); CHECK(read_eq("big.bin", big, sizeof(big)));
    CHECK(mimir_write("big.bin", big, MIMIR_FILE_MAX + 1u) != 0); CHECK(mimir_write("", a, 1) != 0); CHECK(mimir_write("has space", a, 1) != 0);
    char name[MIMIR_NAME_MAX]; uint32_t len, n = 0; while(mimir_list(n, name, sizeof(name), &len) == 0) ++n;
    CHECK(n == 3);
    uint32_t before = used(); CHECK(mimir_write("copy.bin", a, sizeof(a)) == 0); CHECK(used() == before + 1u);   /* dedupe: only the rune record */
    before = used(); CHECK(mimir_write("copy.bin", a, sizeof(a)) == 0); CHECK(used() == before);                 /* identical rewrite is a no-op */
    struct mimir_info in; CHECK(mimir_stat("a.bin", &in) == 0 && in.size == sizeof(a) && in.version == 1u && in.chunks == 15u);
    uint8_t tiny[4]; CHECK(mimir_read("a.bin", tiny, 4, &len) != 0);                                              /* capacity too small */
    CHECK(mimir_read("nope", tiny, 4, &len) != 0);
}
static void test_versions(void) {
    fresh(); static uint8_t v[3][9000];
    for(int i = 0; i < 3; ++i) { fill(v[i], sizeof(v[i]), 10u + (uint32_t)i); CHECK(mimir_write("doc", v[i], sizeof(v[i])) == 0); }
    struct mimir_ver h; for(uint32_t i = 0; i < 3; ++i) { CHECK(mimir_history("doc", i, &h) == 0 && h.version == 3u - i); }
    CHECK(mimir_history("doc", 3, &h) != 0);
    CHECK(mimir_rollback("doc", 1) == 0); CHECK(read_eq("doc", v[0], sizeof(v[0])));
    struct mimir_info in; CHECK(mimir_stat("doc", &in) == 0 && in.version == 4u);
    CHECK(mimir_rollback("doc", 99) != 0);
    CHECK(mimir_remove("doc") == 0); CHECK(!read_eq("doc", v[0], 1)); CHECK(mimir_stat("doc", &in) != 0);
    CHECK(mimir_history("doc", 0, &h) == 0 && (h.flags & 1u) && h.version == 5u);
    CHECK(mimir_write("doc", v[2], sizeof(v[2])) == 0); CHECK(mimir_stat("doc", &in) == 0 && in.version == 6u); CHECK(read_eq("doc", v[2], sizeof(v[2])));
}
static void test_labels(void) {
    fresh(); CHECK(mimir_write_labeled("system.cfg", "x=1", 3, 0x4, 0x40) == 0);
    struct mimir_info in; CHECK(mimir_stat("system.cfg", &in) == 0 && in.read_perm == 0x4 && in.write_perm == 0x40);
    CHECK(mimir_write("system.cfg", "x=2", 3) == 0); CHECK(mimir_stat("system.cfg", &in) == 0 && in.write_perm == 0x40 && in.version == 2u);   /* labels survive new versions */
    CHECK(mimir_set_labels("system.cfg", 0, 0x80) == 0); CHECK(mimir_stat("system.cfg", &in) == 0 && in.read_perm == 0 && in.write_perm == 0x80 && in.version == 3u);
    CHECK(read_eq("system.cfg", (const uint8_t *)"x=2", 3));
    CHECK(mimir_mount(0, base_lba) == 0); CHECK(mimir_stat("system.cfg", &in) == 0 && in.write_perm == 0x80);
}
static void test_wards(void) {
    fresh();
    uint32_t rp = 9, wp = 9;
    CHECK(mimir_ward_lookup("x.sapp", &rp, &wp) == 0);                                       /* no rules file: nothing matches */
    const char *rules = "# comment\n*.sapp 0x0 0x80\nsystem.cfg 0 64\n  secret.txt   0x400 0x400  \nbad line\n*.log 0xZZ 1\n";
    CHECK(mimir_write("ward.rules", rules, (uint32_t)strlen(rules)) == 0);
    CHECK(mimir_ward_lookup("tool.sapp", &rp, &wp) == 1 && rp == 0 && wp == 0x80);
    CHECK(mimir_ward_lookup("system.cfg", &rp, &wp) == 1 && wp == 64u);
    CHECK(mimir_ward_lookup("secret.txt", &rp, &wp) == 1 && rp == 0x400 && wp == 0x400);
    CHECK(mimir_ward_lookup("a.log", &rp, &wp) == 0 && mimir_ward_lookup("notes.txt", &rp, &wp) == 0);   /* malformed lines are ignored */
    CHECK(mimir_ward_lookup("sapp", &rp, &wp) == 0);                                          /* "*.sapp" needs the dot */
    CHECK(mimir_write("tool.sapp", "x", 1) == 0);
    struct mimir_info in; CHECK(mimir_stat("tool.sapp", &in) == 0 && in.write_perm == 0x80);   /* new files take the ward labels */
    CHECK(mimir_write("plain.txt", "x", 1) == 0 && mimir_stat("plain.txt", &in) == 0 && in.write_perm == 0 && in.read_perm == 0);
    CHECK(mimir_write_labeled("tool2.sapp", "x", 1, 1, 2) == 0 && mimir_stat("tool2.sapp", &in) == 0 && in.read_perm == 1 && in.write_perm == 2);   /* explicit labels win */
    CHECK(mimir_set_labels("tool.sapp", 0, 0) == 0); CHECK(mimir_write("tool.sapp", "y", 1) == 0 && mimir_stat("tool.sapp", &in) == 0 && in.write_perm == 0);   /* live files keep their own labels */
    CHECK(mimir_remove("tool.sapp") == 0); CHECK(mimir_write("tool.sapp", "z", 1) == 0 && mimir_stat("tool.sapp", &in) == 0 && in.write_perm == 0x80);   /* re-created: the ward applies again */
    CHECK(mimir_mount(0, base_lba) == 0); CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 1);   /* rules survive a remount */
    CHECK(mimir_ebb() == 0); CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 1);                 /* and an ebb */
    CHECK(mimir_write("ward.rules", "*.txt 0x0 0x2\n", 14) == 0);                              /* the rules are an ordinary versioned file */
    CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 0 && mimir_ward_lookup("b.txt", &rp, &wp) == 1 && wp == 2u);
    CHECK(mimir_rollback("ward.rules", 1) == 0); CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 1 && wp == 0x80);   /* rolling the file back rolls the policy back */
    CHECK(mimir_remove("ward.rules") == 0); CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 0);
    static char big[2000]; memset(big, ' ', sizeof(big)); memcpy(big, "*.sapp 0 1\n", 12);
    CHECK(mimir_write("ward.rules", big, sizeof(big)) == 0); CHECK(mimir_ward_lookup("q.sapp", &rp, &wp) == 0);   /* oversized rules grant nothing */
}
static void test_remount_and_seal(void) {
    static uint8_t a[20000]; fill(a, sizeof(a), 5); fresh(); CHECK(mimir_write("a", a, sizeof(a)) == 0); CHECK(mimir_write("b", "bb", 2) == 0);
    struct mimir_seal s1, s2; CHECK(mimir_seal_info(&s1) == 0); CHECK(mimir_mount(0, base_lba) == 0); CHECK(mimir_seal_info(&s2) == 0);
    CHECK(memcmp(&s1, &s2, sizeof(s1)) == 0); CHECK(s1.generation == 3u && s1.files == 2u && s1.versions == 2u);
    CHECK(read_eq("a", a, sizeof(a)));
    struct mimir_scrub sc; CHECK(mimir_scrub(&sc) == 0 && sc.bad == 0 && sc.blocks == s1.used_blocks);
}
static void test_corruption(void) {
    static uint8_t a[20000], b[20000]; fill(a, sizeof(a), 7); fresh(); CHECK(mimir_write("a", a, sizeof(a)) == 0);
    uint8_t *backup = malloc((size_t)ram_sectors * 512u); memcpy(backup, ram, (size_t)ram_sectors * 512u);
    for(uint32_t blk = 0; blk < 6; ++blk) {   /* flip one bit in every block of the log, one at a time */
        memcpy(ram, backup, (size_t)ram_sectors * 512u);
        size_t off = ((size_t)base_lba + 8u * (1u + blk)) * 512u + 200u; ram[off] ^= 0x10;
        struct mimir_scrub sc; uint32_t len; int mounted = mimir_mount(0, base_lba) == 0;
        if(!mounted) continue;                 /* the damage broke the rune/chain checks while mounting: also a detection */
        CHECK(mimir_scrub(&sc) == 0 && sc.bad >= 1u && sc.first_bad == blk);
        CHECK(mimir_read("a", b, sizeof(b), &len) == MIMIR_E_CORRUPT);
    }
    memcpy(ram, backup, (size_t)ram_sectors * 512u); CHECK(mimir_mount(0, base_lba) == 0); CHECK(read_eq("a", a, sizeof(a)));
    free(backup);
}
static void test_torn_writes(void) {
    static uint8_t a[30000], b[30000]; fill(a, sizeof(a), 21); fill(b, sizeof(b), 22);
    fresh(); CHECK(mimir_write("f", a, sizeof(a)) == 0);
    uint8_t *backup = malloc((size_t)ram_sectors * 512u); memcpy(backup, ram, (size_t)ram_sectors * 512u);
    int old_seen = 0, new_seen = 0;
    for(long n = 0; n < 80; ++n) {            /* power fails after n sector writes of an update */
        memcpy(ram, backup, (size_t)ram_sectors * 512u); CHECK(mimir_mount(0, base_lba) == 0);
        writes_left = n; int rc = mimir_write("f", b, sizeof(b)); writes_left = -1;
        CHECK(mimir_mount(0, base_lba) == 0);
        struct mimir_scrub sc; CHECK(mimir_scrub(&sc) == 0 && sc.bad == 0);
        if(read_eq("f", a, sizeof(a))) { ++old_seen; CHECK(rc != 0); } else { CHECK(read_eq("f", b, sizeof(b))); ++new_seen; }
    }
    CHECK(old_seen > 0 && new_seen > 0);      /* the cut-over is atomic: old until the seal lands, then new */
    memcpy(ram, backup, (size_t)ram_sectors * 512u); CHECK(mimir_mount(0, base_lba) == 0); CHECK(mimir_write("f", b, sizeof(b)) == 0);
    uint8_t s[512]; size_t seal_off = ((size_t)base_lba + (3u & 1u)) * 512u;   /* newest seal is generation 3 -> slot 1... */
    struct mimir_seal si; CHECK(mimir_seal_info(&si) == 0); seal_off = ((size_t)base_lba + (si.generation & 1u)) * 512u;
    memcpy(s, ram + seal_off, 512u); ram[seal_off + 70] ^= 1;                                      /* tear the newest seal */
    CHECK(mimir_mount(0, base_lba) == 0); CHECK(read_eq("f", a, sizeof(a)));                       /* falls back one generation */
    free(backup);
}
static void test_ebb(void) {
    fresh(); static uint8_t keep[7000], tmp[16000]; fill(keep, sizeof(keep), 99);
    CHECK(mimir_write("keep", keep, sizeof(keep)) == 0); CHECK(mimir_write_labeled("lab", "x", 1, 0, 0x80) == 0);
    struct mimir_seal s; CHECK(mimir_seal_info(&s) == 0 && s.epoch == 0u && s.tide == 0u);
    uint32_t i = 0;
    for(; i < 400; ++i) { fill(tmp, sizeof(tmp), 1000u + i); char nm[16]; snprintf(nm, sizeof(nm), "t%u", i % 5u); CHECK(mimir_write(nm, tmp, sizeof(tmp)) == 0); if(failures) break; }
    CHECK(mimir_seal_info(&s) == 0 && s.epoch >= 1u);                    /* the tide filled and the ebb ran by itself */
    CHECK(read_eq("keep", keep, sizeof(keep))); struct mimir_info in; CHECK(mimir_stat("lab", &in) == 0 && in.write_perm == 0x80);
    fill(tmp, sizeof(tmp), 1000u + (i - 1u)); char last[16]; snprintf(last, sizeof(last), "t%u", (i - 1u) % 5u); CHECK(read_eq(last, tmp, sizeof(tmp)));
    struct mimir_scrub sc; CHECK(mimir_scrub(&sc) == 0 && sc.bad == 0);
    uint32_t e = s.epoch, v; CHECK(mimir_stat("keep", &in) == 0); v = in.version; uint8_t id[32]; memcpy(id, in.id, 32);
    CHECK(mimir_ebb() == 0); CHECK(mimir_seal_info(&s) == 0 && s.epoch == e + 1u);
    CHECK(mimir_stat("keep", &in) == 0 && in.version == v && memcmp(in.id, id, 32) == 0);   /* content-addressed: ids survive the ebb */
    CHECK(mimir_mount(0, base_lba) == 0); CHECK(read_eq("keep", keep, sizeof(keep)));
    CHECK(mimir_remove("keep") == 0); CHECK(mimir_ebb() == 0); CHECK(mimir_stat("keep", &in) != 0);   /* removed files are not carried over */
}
static void test_crash_during_ebb(void) {
    fresh(); static uint8_t a[12000]; fill(a, sizeof(a), 3); CHECK(mimir_write("a", a, sizeof(a)) == 0); CHECK(mimir_write("b", "b", 1) == 0);
    uint8_t *backup = malloc((size_t)ram_sectors * 512u); memcpy(backup, ram, (size_t)ram_sectors * 512u);
    for(long n = 0; n < 40; n += 3) {
        memcpy(ram, backup, (size_t)ram_sectors * 512u); CHECK(mimir_mount(0, base_lba) == 0);
        writes_left = n; (void)mimir_ebb(); writes_left = -1;
        CHECK(mimir_mount(0, base_lba) == 0); CHECK(read_eq("a", a, sizeof(a))); CHECK(read_eq("b", (const uint8_t *)"b", 1));
        struct mimir_scrub sc; CHECK(mimir_scrub(&sc) == 0 && sc.bad == 0);
    }
    free(backup);
}
static int image_check(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb"); if(!f) { perror("image"); return 1; }
    fseek(f, 0, SEEK_END); long size = ftell(f); fseek(f, 0, SEEK_SET); ram_sectors = (uint32_t)(size / 512); free(ram); ram = malloc((size_t)size);
    if(fread(ram, 1, (size_t)size, f) != (size_t)size) return 1;
    fclose(f);
    mutable_disk = disk; mutable_disk.sector_count = ram_sectors; (void)mutable_disk;
    uint32_t start = (uint32_t)strtoul(argv[2], 0, 0);
    CHECK(mimir_mount(0, start) == 0);
    for(int i = 4; i < argc; ++i) {
        char *eq = strchr(argv[i], '='); if(!eq) continue; *eq = 0; FILE *g = fopen(eq + 1, "rb"); if(!g) { perror(eq + 1); return 1; }
        static uint8_t want[MIMIR_FILE_MAX]; size_t n = fread(want, 1, sizeof(want), g); fclose(g);
        CHECK(read_eq(argv[i], want, (uint32_t)n));
        struct mimir_info in; CHECK(mimir_stat(argv[i], &in) == 0 && in.version == 1u);
        if(strstr(argv[i], ".sapp")) CHECK(in.write_perm == 0x80);
    }
    { uint32_t r, w; CHECK(mimir_ward_lookup("any.sapp", &r, &w) == 1 && w == 0x80); CHECK(mimir_ward_lookup("system.cfg", &r, &w) == 1 && w == 0x40); }   /* the image ships the default wards */
    struct mimir_scrub sc; CHECK(mimir_scrub(&sc) == 0 && sc.bad == 0 && sc.blocks > 0);
    CHECK(mimir_write("host-written.txt", "later", 5) == 0); CHECK(mimir_mount(0, start) == 0);   /* the kernel code can extend a Python-built volume */
    printf("mimir image check: %s\n", failures ? "FAILED" : "ok"); return failures != 0;
}
int main(int argc, char **argv) {
    ram_sectors = 8192u + 64u; ram = malloc((size_t)ram_sectors * 512u); base_lba = 16u;
    mutable_disk = disk; mutable_disk.sector_count = ram_sectors;
    if(block_register(&mutable_disk) != 0) return 2;
    if(argc >= 4) return image_check(argc, argv);
    test_basic(); test_versions(); test_labels(); test_wards(); test_remount_and_seal(); test_corruption(); test_torn_writes(); test_ebb(); test_crash_during_ebb();
    printf("mimir host tests: %s\n", failures ? "FAILED" : "ok"); free(ram); return failures != 0;
}
