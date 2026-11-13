#include "sand/crypto/ed25519.h"
#include "sand/crypto/sha512.h"
#include "crypto_consts.h"

/* Field GF(2^255-19) in 16 signed 16-bit-radix limbs held in int64_t, point
 * arithmetic in extended twisted-Edwards coordinates (X,Y,Z,T). Only 64-bit
 * add/sub/mul/shift are used, so no libgcc helper is needed on i386. */
typedef int64_t gf[16];

static void set25519(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }

static void car25519(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}
static void sel25519(gf p, gf q, int64_t b) {
    int64_t c = ~(b - 1);
    for (int i = 0; i < 16; ++i) { int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
static void pack25519(uint8_t *o, const gf n) {
    gf m, t; set25519(t, n);
    car25519(t); car25519(t); car25519(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i-1] >> 16) & 1); m[i-1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int64_t b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) { o[2*i] = (uint8_t)(t[i] & 0xff); o[2*i+1] = (uint8_t)(t[i] >> 8); }
}
static int neq25519(const gf a, const gf b) {
    uint8_t c[32], d[32]; pack25519(c, a); pack25519(d, b);
    uint8_t x = 0; for (int i = 0; i < 32; ++i) x |= c[i] ^ d[i];
    return x != 0;
}
static int par25519(const gf a) { uint8_t d[32]; pack25519(d, a); return d[0] & 1; }
static void unpack25519(gf o, const uint8_t *n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2*i] + ((int64_t)n[2*i+1] << 8);
    o[15] &= 0x7fff;
}
static void fadd(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
static void fsub(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }
static void fmul(gf o, const gf a, const gf b) {
    int64_t t[31];
    for (int i = 0; i < 31; ++i) t[i] = 0;
    for (int i = 0; i < 16; ++i) for (int j = 0; j < 16; ++j) t[i+j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i+16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    car25519(o); car25519(o);
}
static void fsqr(gf o, const gf a) { fmul(o, a, a); }
static void inv25519(gf o, const gf i) {
    gf c; set25519(c, i);
    for (int a = 253; a >= 0; --a) { fsqr(c, c); if (a != 2 && a != 4) fmul(c, c, i); }
    set25519(o, c);
}
static void pow2523(gf o, const gf i) {
    gf c; set25519(c, i);
    for (int a = 250; a >= 0; --a) { fsqr(c, c); if (a != 1) fmul(c, c, i); }
    set25519(o, c);
}

static void point_add(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    fsub(a, p[1], p[0]); fsub(t, q[1], q[0]); fmul(a, a, t);
    fadd(b, p[0], p[1]); fadd(t, q[0], q[1]); fmul(b, b, t);
    fmul(c, p[3], q[3]); fmul(c, c, GF_D2);
    fmul(d, p[2], q[2]); fadd(d, d, d);
    fsub(e, b, a); fsub(f, d, c); fadd(g, d, c); fadd(h, b, a);
    fmul(p[0], e, f); fmul(p[1], h, g); fmul(p[2], g, f); fmul(p[3], e, h);
}
static void cswap(gf p[4], gf q[4], uint8_t b) { for (int i = 0; i < 4; ++i) sel25519(p[i], q[i], b); }
static void point_pack(uint8_t *r, gf p[4]) {
    gf tx, ty, zi;
    inv25519(zi, p[2]); fmul(tx, p[0], zi); fmul(ty, p[1], zi);
    pack25519(r, ty); r[31] ^= (uint8_t)(par25519(tx) << 7);
}
static void scalarmult(gf p[4], gf q[4], const uint8_t *s) {
    for (int i = 0; i < 16; ++i) { p[0][i] = 0; p[1][i] = (i == 0); p[2][i] = (i == 0); p[3][i] = 0; }
    for (int i = 255; i >= 0; --i) {
        uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        cswap(p, q, b); point_add(q, p); point_add(p, p); cswap(p, q, b);
    }
}
static void scalarbase(gf p[4], const uint8_t *s) {
    gf q[4];
    set25519(q[0], GF_X); set25519(q[1], GF_Y);
    for (int i = 0; i < 16; ++i) { q[2][i] = (i == 0); }
    fmul(q[3], GF_X, GF_Y);
    scalarmult(p, q, s);
}
/* Decode a point and return its NEGATION (so verification is one addition). */
static int unpackneg(gf r[4], const uint8_t *p) {
    /* canonical y: reject y >= 2^255-19 */
    int canon_hi = ((p[31] & 0x7f) == 0x7f) && (p[0] >= 0xed);
    for (int i = 1; i < 31 && canon_hi; ++i) if (p[i] != 0xff) canon_hi = 0;
    if (canon_hi) return -1;
    gf t, chk, num, den, den2, den4, den6;
    for (int i = 0; i < 16; ++i) r[2][i] = (i == 0);
    unpack25519(r[1], p);
    fsqr(num, r[1]); fmul(den, num, GF_D); fsub(num, num, r[2]); fadd(den, r[2], den);
    fsqr(den2, den); fsqr(den4, den2); fmul(den6, den4, den2);
    fmul(t, den6, num); fmul(t, t, den);
    pow2523(t, t); fmul(t, t, num); fmul(t, t, den); fmul(t, t, den); fmul(r[0], t, den);
    fsqr(chk, r[0]); fmul(chk, chk, den);
    if (neq25519(chk, num)) fmul(r[0], r[0], GF_I);
    fsqr(chk, r[0]); fmul(chk, chk, den);
    if (neq25519(chk, num)) return -1;
    uint8_t xz[32]; pack25519(xz, r[0]);
    uint8_t zero = 0; for (int i = 0; i < 32; ++i) zero |= xz[i];
    if (zero == 0 && (p[31] >> 7)) return -1; /* x==0 with sign bit set is invalid */
    if (par25519(r[0]) == (p[31] >> 7)) { gf z0; for (int i = 0; i < 16; ++i) z0[i] = 0; fsub(r[0], z0, r[0]); }
    fmul(r[3], r[0], r[1]);
    return 0;
}

static const int64_t L[32] = {0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
                              0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10};
static void mod_l(uint8_t *r, int64_t x[64]) {
    int64_t carry; int i, j;
    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry; x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j) { x[j] += carry - (x[31] >> 4) * L[j]; carry = x[j] >> 8; x[j] &= 255; }
    for (j = 0; j < 32; ++j) x[j] -= carry * L[j];
    for (i = 0; i < 32; ++i) { x[i+1] += x[i] >> 8; r[i] = (uint8_t)(x[i] & 255); }
}
static void reduce64(uint8_t r[64]) {
    int64_t x[64];
    for (int i = 0; i < 64; ++i) x[i] = r[i];
    for (int i = 0; i < 64; ++i) r[i] = 0;
    mod_l(r, x);
}
/* 1 when little-endian s < L */
static int scalar_canonical(const uint8_t s[32]) {
    for (int i = 31; i >= 0; --i) {
        if (s[i] < (uint8_t)L[i]) return 1;
        if (s[i] > (uint8_t)L[i]) return 0;
    }
    return 0; /* equal */
}

int sand_ed25519_verify2(const uint8_t sig[64], const uint8_t pub[32],
                         const void *part1, size_t len1, const void *part2, size_t len2) {
    if (!sig || !pub || (len1 && !part1) || (len2 && !part2)) return 0;
    if (!scalar_canonical(sig + 32)) return 0;
    gf q[4], p[4];
    if (unpackneg(q, pub) != 0) return 0;
    uint8_t h[64];
    struct sand_sha512 c;
    sand_sha512_init(&c);
    sand_sha512_update(&c, sig, 32);
    sand_sha512_update(&c, pub, 32);
    if (len1) sand_sha512_update(&c, part1, len1);
    if (len2) sand_sha512_update(&c, part2, len2);
    sand_sha512_final(&c, h);
    reduce64(h);
    scalarmult(p, q, h);
    gf b[4];
    scalarbase(b, sig + 32);
    point_add(p, b);
    uint8_t t[32];
    point_pack(t, p);
    uint8_t diff = 0;
    for (int i = 0; i < 32; ++i) diff |= t[i] ^ sig[i];
    return diff == 0;
}

int sand_ed25519_verify(const uint8_t sig[64], const uint8_t pub[32], const void *msg, size_t len) {
    return sand_ed25519_verify2(sig, pub, msg, len, 0, 0);
}
