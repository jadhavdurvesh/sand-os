#!/usr/bin/env python3
"""Derive the SHA-512 and Ed25519 constants used by kernel/crypto from first
principles (no copied tables), so they can be re-checked by anyone."""
import sys
P = 2**255 - 19
def primes(n):
    out, c = [], 2
    while len(out) < n:
        if all(c % q for q in out): out.append(c)
        c += 1
    return out
def iroot(n, k):
    lo, hi = 0, 1 << (n.bit_length() // k + 2)
    while lo < hi:
        m = (lo + hi + 1) // 2
        if m ** k <= n: lo = m
        else: hi = m - 1
    return lo
M64 = (1 << 64) - 1
H0 = [iroot(p << 128, 2) & M64 for p in primes(8)]
K = [iroot(p << 192, 3) & M64 for p in primes(80)]
def limbs(v): return [(v >> (16 * i)) & 0xFFFF for i in range(16)]
inv = lambda a: pow(a, P - 2, P)
D = (-121665 * inv(121666)) % P
I = pow(2, (P - 1) // 4, P)
By = (4 * inv(5)) % P
def recover_x(y):
    x2 = (y * y - 1) * inv(D * y * y + 1) % P
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P: x = x * I % P
    return x if x % 2 == 0 else P - x
Bx = recover_x(By)
def arr(name, vals, w):
    print(f"static const uint64_t {name}[] = {{" if w == 64 else f"static const int64_t {name}[16] = {{")
    print("    " + ", ".join(f"0x{v:0{16 if w==64 else 4}x}" + ("ULL" if w == 64 else "") for v in vals) + "};")
if __name__ == "__main__":
    print("/* sha512 H0 */"); arr("SHA512_H0", H0, 64)
    print("/* sha512 K */");  arr("SHA512_K", K, 64)
    for n, v in (("GF_D", D), ("GF_D2", 2 * D % P), ("GF_X", Bx), ("GF_Y", By), ("GF_I", I)): arr(n, limbs(v), 16)
