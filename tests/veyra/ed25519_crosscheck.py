#!/usr/bin/env python3
"""Cross-check the SAND verifier against an independent Ed25519 implementation
(python 'cryptography'): random valid signatures must verify, mutated ones must not.
Skips cleanly when 'cryptography' is unavailable."""
import os, random, subprocess, sys, tempfile
try:
    from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
    from cryptography.hazmat.primitives import serialization as ser
except ImportError:
    print("cryptography not installed; skipping"); sys.exit(0)
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
cli_src = r'''
#include <stdio.h>
#include <stdlib.h>
#include "sand/crypto/ed25519.h"
int main(void){ unsigned char pub[32],sig[64]; static unsigned char msg[70000]; char line[1<<18];
 while(fgets(line,sizeof line,stdin)){ unsigned v; size_t n=0; char*p=line;
  for(int i=0;i<32;i++){sscanf(p,"%2x",&v);pub[i]=v;p+=2;} for(int i=0;i<64;i++){sscanf(p,"%2x",&v);sig[i]=v;p+=2;}
  while(sscanf(p,"%2x",&v)==1){msg[n++]=v;p+=2;}
  putchar(sand_ed25519_verify(sig,pub,msg,n)?'1':'0'); } putchar('\n'); return 0; }
'''
d = tempfile.mkdtemp()
open(d + "/cli.c", "w").write(cli_src)
subprocess.check_call(["cc", "-O2", "-I", root + "/include", "-o", d + "/cli", d + "/cli.c",
                       root + "/kernel/crypto/ed25519.c", root + "/kernel/crypto/sha512.c", "-I", root + "/kernel/crypto"])
rnd = random.Random(1234)
cases, expect = [], []
for i in range(150):
    k = Ed25519PrivateKey.generate()
    pub = k.public_key().public_bytes(ser.Encoding.Raw, ser.PublicFormat.Raw)
    msg = bytes(rnd.getrandbits(8) for _ in range(rnd.choice([0, 1, 31, 32, 111, 112, 113, 127, 128, 129, 500, 4000])))
    sig = k.sign(msg)
    cases.append((pub, sig, msg)); expect.append("1")
    m2 = bytearray(msg + b"x"); m2[rnd.randrange(len(m2))] ^= 1 << rnd.randrange(8)
    cases.append((pub, sig, bytes(m2))); expect.append("0")
    s2 = bytearray(sig); s2[rnd.randrange(64)] ^= 1 << rnd.randrange(8)
    cases.append((pub, bytes(s2), msg)); expect.append("0")
inp = "".join((p + s + m).hex() + "\n" for p, s, m in cases)
out = subprocess.run([d + "/cli"], input=inp, capture_output=True, text=True, check=True).stdout.strip()
bad = [i for i, (a, b) in enumerate(zip(out, expect)) if a != b]
if bad or len(out) != len(expect):
    print("MISMATCH at cases", bad[:10]); sys.exit(1)
print(f"ed25519 cross-check passed ({len(cases)} cases)")
