/* Host helper: stci_verify_cli <image> <trust.bin> <now-or-"-"> <policy-flags>  -> prints status, key ids. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sand/security/stci.h"

static uint8_t *slurp(const char *p, uint32_t *n) {
    FILE *f = fopen(p, "rb"); if (!f) return 0;
    fseek(f, 0, SEEK_END); long l = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(l ? (size_t)l : 1); if (l && fread(b, 1, (size_t)l, f) != (size_t)l) { fclose(f); free(b); return 0; }
    fclose(f); *n = (uint32_t)l; return b;
}
int main(int argc, char **argv) {
    if (argc < 5) return 99;
    uint32_t in = 0, tn = 0; uint8_t *img = slurp(argv[1], &in), *tb = slurp(argv[2], &tn);
    if (!img || !tb || tn < 8) return 98;
    uint32_t rc; memcpy(&rc, tb, 4);
    struct sand_stci_root roots[SAND_STCI_MAX_ROOTS]; uint32_t off = 4;
    if (rc > SAND_STCI_MAX_ROOTS) return 97;
    for (uint32_t i = 0; i < rc; ++i) { memcpy(roots[i].key_id, tb + off, 32); memcpy(roots[i].public_key, tb + off + 32, 32); memcpy(&roots[i].flags, tb + off + 64, 4); off += 68; }
    uint32_t rv; memcpy(&rv, tb + off, 4); off += 4;
    const uint8_t (*revoked)[32] = (const uint8_t (*)[32])(tb + off);
    struct sand_stci_trust_store ts = { SAND_STCI_ABI_MAJOR, sizeof ts, rc, 1, roots, rv ? revoked : 0, rv };
    struct sand_stci_policy pol = { SAND_STCI_ABI_MAJOR, sizeof pol, (uint32_t)strtoul(argv[4], 0, 0), 0, strcmp(argv[3], "-") ? argv[3] : 0 };
    uint8_t digest[32]; struct sand_stci_result res; memset(&res, 0, sizeof res);
    int st = sand_stci_verify_sapp_v2_ex(img, in, &ts, &pol, digest, &res);
    printf("%d\n", st);
    free(img); free(tb);
    return 0;
}
