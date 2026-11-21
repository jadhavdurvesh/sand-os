/* Host test for diskapps_manifest_permissions(): real SAPP images + hostile metadata. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sand/proc/diskapps.h"
#include "sand/kernel/perms.h"
#include "sand/fs/mimir.h"
int mimir_is_mounted(void) { return 0; }
int mimir_read(const char *n, void *d, uint32_t c, uint32_t *l) { (void)n; (void)d; (void)c; (void)l; return -1; }
int mimir_list(uint32_t o, char *n, uint32_t c, uint32_t *l) { (void)o; (void)n; (void)c; (void)l; return -1; }
void *kmalloc(uint32_t s) { return malloc(s); }

#include "sand/proc/apps.h"
static int failures;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++failures; } } while (0)

/* Minimal image: 96-byte header with metadata at offset 96. */
static uint32_t with_meta(const char *json) {
    static uint8_t img[4096]; uint32_t n = (uint32_t)strlen(json);
    memset(img, 0, sizeof img);
    uint32_t off = 96; memcpy(img + 28, &off, 4); memcpy(img + 32, &n, 4);
    memcpy(img + off, json, n);
    return diskapps_manifest_permissions(img, off + n);
}
int main(int argc, char **argv) {
    CHECK(with_meta("{\"id\":\"a\",\"permissions\":[\"console.write\"],\"version\":\"1\"}") == SAND_PERM_CONSOLE_WRITE, "single");
    CHECK(with_meta("{\"permissions\":[\"fs.rw\",\"window.create\",\"settings.write\"]}") == (SAND_PERM_FS_READ | SAND_PERM_FS_WRITE | SAND_PERM_WINDOW | SAND_PERM_SETTINGS), "aliases");
    CHECK(with_meta("{\"permissions\":[]}") == 0, "empty list");
    CHECK(with_meta("{\"id\":\"a\"}") == 0, "no permissions key");
    CHECK(with_meta("{\"permissions\":[\"nonsense\",\"fs.read\"]}") == SAND_PERM_FS_READ, "unknown names grant nothing");
    CHECK(with_meta("{\"description\":\"\\\"permissions\\\":[\\\"fs.rw\\\"]\",\"id\":\"a\"}") == 0, "key inside an escaped string is not a key");
    CHECK(with_meta("{\"nested\":{\"permissions\":[\"fs.rw\"]},\"id\":\"a\"}") == 0, "only the top-level key counts");
    CHECK(with_meta("{\"permissions\":[\"console.write\"") == 0, "truncated array grants nothing");
    CHECK(with_meta("{\"permissions\":\"fs.rw\"}") == 0, "non-array grants nothing");
    CHECK(with_meta("{\"permissions\":[\"fs\\u002erw\"]}") == 0, "escapes rejected");
    CHECK(with_meta("{\"permissions\":[1]}") == 0, "non-string element");
    CHECK(diskapps_manifest_permissions(NULL, 0) == 0, "null image");
    uint8_t tiny[40] = {0}; CHECK(diskapps_manifest_permissions(tiny, sizeof tiny) == 0, "short image");
    {   /* launcher block */
        static uint8_t img[4096]; struct sand_app_launcher_info li; uint32_t order = 0;
        const char *good = "{\"launcher\":{\"accent\":\"#6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"L\",\"label\":\"LUMEN\",\"order\":50,\"subtitle\":\"Light field\"}}";
        const char *bad[] = {
            "{\"launcher\":{\"accent\":\"#6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"L\",\"label\":\"LUMEN\",\"subtitle\":\"x\"}}",            /* no order */
            "{\"launcher\":{\"accent\":\"6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"L\",\"label\":\"L\",\"order\":1,\"subtitle\":\"x\"}}",              /* colour without # */
            "{\"launcher\":{\"accent\":\"#6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"LL\",\"label\":\"L\",\"order\":1,\"subtitle\":\"x\"}}",            /* glyph too long */
            "{\"launcher\":{\"accent\":\"#6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"L\",\"label\":\"0123456789ABCDEF\",\"order\":1,\"subtitle\":\"x\"}}", /* label 16 chars */
            "{\"launcher\":{\"accent\":\"#6FC3FF\",\"accent2\":\"#2A5FA8\",\"glyph\":\"L\",\"label\":\"L\",\"order\":99999,\"subtitle\":\"x\"}}",         /* order range */
            "{\"launcher\":{\"evil\":\"x\"}}", "{\"launcher\":[1]}", "{\"x\":{\"launcher\":{}}}", "{\"launcher\":{\"label\":\"L"};
        uint32_t n = (uint32_t)strlen(good); uint32_t off = 96;
        memset(img, 0, sizeof img); memcpy(img + 28, &off, 4); memcpy(img + 32, &n, 4); memcpy(img + off, good, n);
        CHECK(diskapps_manifest_launcher(img, off + n, "lumen", &li, &order) == 0, "launcher parses");
        CHECK(order == 50 && li.accent == 0x6FC3FFu && li.accent2 == 0x2A5FA8u && li.glyph == 'L' && !strcmp(li.label, "LUMEN") && !strcmp(li.subtitle, "Light field") && !strcmp(li.app, "lumen"), "launcher fields");
        for (unsigned k = 0; k < sizeof bad / sizeof bad[0]; ++k) {
            uint32_t m = (uint32_t)strlen(bad[k]);
            memset(img, 0, sizeof img); memcpy(img + 28, &off, 4); memcpy(img + 32, &m, 4); memcpy(img + off, bad[k], m);
            CHECK(diskapps_manifest_launcher(img, off + m, "lumen", &li, &order) != 0, bad[k]);
        }
    }
    for (int i = 1; i < argc; i += 2) {              /* <file.sapp> <expected mask> pairs from real builds */
        FILE *f = fopen(argv[i], "rb"); uint8_t *b = malloc(1 << 17); size_t n = f ? fread(b, 1, 1 << 17, f) : 0;
        if (f) fclose(f);
        uint32_t want = (uint32_t)strtoul(argv[i + 1], 0, 0);
        CHECK(n > 0 && diskapps_manifest_permissions(b, (uint32_t)n) == want, argv[i]);
        free(b);
    }
    puts(failures ? "diskapps_perm_test: FAILED" : "diskapps_perm_test: OK");
    return failures != 0;
}
