#include "sand/audio/gjallar.h"
static const int16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143,
    157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552,
    1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
static const int8_t index_table[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
void gjallar_decode_block(const uint8_t *block, int16_t *out) {
    for (uint32_t ch = 0; ch < 2u; ++ch) {
        int32_t pred = (int16_t)((uint32_t)block[ch * 4u] | ((uint32_t)block[ch * 4u + 1u] << 8));
        int32_t index = block[ch * 4u + 2u]; if (index > 88) index = 88;
        const uint8_t *p = block + 8u + ch * 4u;
        uint32_t n = 0;
        for (uint32_t group = 0; group < GJLR_BLOCK_FRAMES / 8u; ++group, p += 8u) {
            for (uint32_t b = 0; b < 4u; ++b) {
                for (uint32_t half = 0; half < 2u; ++half) {
                    uint32_t nib = half ? (uint32_t)(p[b] >> 4) : (uint32_t)(p[b] & 15u);
                    int32_t step = step_table[index], delta = step >> 3;
                    if (nib & 4u) delta += step;
                    if (nib & 2u) delta += step >> 1;
                    if (nib & 1u) delta += step >> 2;
                    pred += (nib & 8u) ? -delta : delta;
                    if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
                    index += index_table[nib & 7u]; if (index < 0) index = 0; else if (index > 88) index = 88;
                    out[n * 2u + ch] = (int16_t)pred; ++n;
                }
            }
        }
    }
}
