/* runetest: exercises sand/rune.h against the live MIMIR store. Prints RUNETEST:PASS or RUNETEST:FAIL <n>. */
#include "sand/libsand.h"
#include "sand/rune.h"
static uint32_t fails;
static void check(int ok, uint32_t n) { if (!ok) { ++fails; sand_puts("RUNETEST:FAIL "); sand_putu(n); sand_puts("\n"); } }
struct tally { uint32_t files, branches; };
static void visit(const char *name, uint32_t length, int is_branch, void *user) {
    struct tally *t = (struct tally *)user; (void)name; (void)length;
    if (is_branch) ++t->branches; else ++t->files;
}
void _start(void)
{
    char buf[32];
    check(sand_rune_put_text("rt/a.txt", "one") >= 0, 1);
    check(sand_rune_version("rt/a.txt") == 1, 2);
    check(sand_rune_put_text("rt/a.txt", "two") >= 0, 3);
    check(sand_rune_version("rt/a.txt") == 2, 4);
    check(sand_rune_put_text("rt/b.txt", "two") >= 0, 5);
    check(sand_rune_same("rt/a.txt", "rt/b.txt", buf, sizeof buf) == 1, 6);                 /* identical content */
    check(sand_rune_undo("rt/a.txt") == 1, 7);                              /* restores version 1 as version 3 */
    check(sand_rune_get("rt/a.txt", buf, sizeof buf) == 3 && memcmp(buf, "one", 3) == 0, 8);
    check(sand_rune_version("rt/a.txt") == 3, 9);
    check(sand_rune_same("rt/a.txt", "rt/b.txt", buf, sizeof buf) == 0, 10);
    check(sand_rune_put_text("rt/deep/c.txt", "c") >= 0, 11);
    struct tally t = {0, 0};
    check(sand_rune_walk("rt/", visit, &t) == 3 && t.files == 2 && t.branches == 1, 12);   /* a.txt, b.txt, deep/ */
    check(sand_rune_version("rt/missing") < 0, 13);
    check(sand_rune_undo("rt/b.txt") < 0, 14);                              /* only one version: nothing earlier */
    check(sand_rune_protect("rt/a.txt", 0, 0x80u) < 0, 15);     /* this app lacks system.install */
    check(sand_rune_healthy() == 1, 16);
    sand_fs_remove("rt/a.txt"); sand_fs_remove("rt/b.txt"); sand_fs_remove("rt/deep/c.txt");
    if (fails == 0) sand_puts("RUNETEST:PASS\n");
    sand_exit(fails ? 1 : 0);
}
