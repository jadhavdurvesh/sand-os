#include "sand/crypto/sha512.h"
#include "crypto_consts.h"

static inline uint64_t ror(uint64_t x, unsigned n) { return (x >> n) | (x << (64u - n)); }

static void compress(uint64_t h[8], const uint8_t *p) {
    uint64_t w[80];
    for (unsigned i = 0; i < 16; ++i) {
        uint64_t v = 0;
        for (unsigned j = 0; j < 8; ++j) v = (v << 8) | p[i * 8 + j];
        w[i] = v;
    }
    for (unsigned i = 16; i < 80; ++i) {
        uint64_t s0 = ror(w[i-15], 1) ^ ror(w[i-15], 8) ^ (w[i-15] >> 7);
        uint64_t s1 = ror(w[i-2], 19) ^ ror(w[i-2], 61) ^ (w[i-2] >> 6);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (unsigned i = 0; i < 80; ++i) {
        uint64_t S1 = ror(e, 14) ^ ror(e, 18) ^ ror(e, 41);
        uint64_t ch = (e & f) ^ (~e & g);
        uint64_t t1 = hh + S1 + ch + SHA512_K[i] + w[i];
        uint64_t S0 = ror(a, 28) ^ ror(a, 34) ^ ror(a, 39);
        uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        uint64_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sand_sha512_init(struct sand_sha512 *c) {
    for (unsigned i = 0; i < 8; ++i) c->h[i] = SHA512_H0[i];
    c->total = 0; c->buflen = 0;
}

void sand_sha512_update(struct sand_sha512 *c, const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    c->total += len;
    while (len) {
        if (c->buflen == 0 && len >= 128) { compress(c->h, p); p += 128; len -= 128; continue; }
        size_t n = 128u - c->buflen; if (n > len) n = len;
        for (size_t i = 0; i < n; ++i) c->buf[c->buflen + i] = p[i];
        c->buflen += (uint32_t)n; p += n; len -= n;
        if (c->buflen == 128) { compress(c->h, c->buf); c->buflen = 0; }
    }
}

void sand_sha512_final(struct sand_sha512 *c, uint8_t out[64]) {
    uint64_t bits_hi = c->total >> 61, bits_lo = c->total << 3;
    c->buf[c->buflen++] = 0x80;
    if (c->buflen > 112) {
        while (c->buflen < 128) c->buf[c->buflen++] = 0;
        compress(c->h, c->buf); c->buflen = 0;
    }
    while (c->buflen < 112) c->buf[c->buflen++] = 0;
    for (unsigned i = 0; i < 8; ++i) { c->buf[112 + i] = (uint8_t)(bits_hi >> (56 - 8 * i)); c->buf[120 + i] = (uint8_t)(bits_lo >> (56 - 8 * i)); }
    compress(c->h, c->buf);
    for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 8; ++j) out[i * 8 + j] = (uint8_t)(c->h[i] >> (56 - 8 * j));
    for (unsigned i = 0; i < sizeof c->buf; ++i) c->buf[i] = 0;
    c->buflen = 0;
}
