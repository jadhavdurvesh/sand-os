/* Host test: the kernel's ADPCM block decoder must reproduce tools/gjallar_pack.py's reference decode bit for bit.
 * usage: gjallar-codec-test gjallar.bin reference.pcm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define GJALLAR_HOST
#include "sand/audio/gjallar.h"
int main(int argc, char **argv) {
    if (argc < 3) return 2;
    FILE *fb = fopen(argv[1], "rb"), *fr = fopen(argv[2], "rb");
    if (!fb || !fr) { fprintf(stderr, "missing input\n"); return 2; }
    uint8_t head[512];
    if (fread(head, 1, 512, fb) != 512 || memcmp(head, "GJLR", 4) != 0) { fprintf(stderr, "bad header\n"); return 1; }
    uint32_t block_count, block_bytes, block_frames, cue; memcpy(&block_frames, head + 20, 4); memcpy(&block_bytes, head + 24, 4); memcpy(&block_count, head + 28, 4); memcpy(&cue, head + 32, 4);
    if (block_frames != GJLR_BLOCK_FRAMES || block_bytes != GJLR_BLOCK_BYTES) { fprintf(stderr, "geometry mismatch\n"); return 1; }
    static uint8_t blk[GJLR_BLOCK_BYTES]; static int16_t out[GJLR_BLOCK_FRAMES * 2], ref[GJLR_BLOCK_FRAMES * 2];
    for (uint32_t b = 0; b < block_count; ++b) {
        if (fread(blk, 1, GJLR_BLOCK_BYTES, fb) != GJLR_BLOCK_BYTES || fread(ref, 2, GJLR_BLOCK_FRAMES * 2, fr) != GJLR_BLOCK_FRAMES * 2) { fprintf(stderr, "short read at block %u\n", b); return 1; }
        gjallar_decode_block(blk, out);
        if (memcmp(out, ref, sizeof out) != 0) { fprintf(stderr, "block %u differs\n", b); return 1; }
    }
    if (cue < 1000u) { fprintf(stderr, "cue missing (%u)\n", cue); return 1; }
    printf("gjallar codec test: ok (%u blocks, cue %u ms)\n", block_count, cue);
    return 0;
}
