#!/usr/bin/env python3
"""Generate SAND STCI test material (roots, publisher certificates, signed SAPP v2 images).

Formats are identical to sand-apps tools/sandcert.py and tools/sapp_sign.py (OpenSSL 3.x Ed25519,
canonical JSON). Keys generated here are throw-away TEST keys and must never be reused or trusted
outside tests: they live in a temp/build directory and are never committed.
"""
import base64, hashlib, json, os, struct, subprocess, sys, tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

DOMAIN = b"SAND-SAPP-V2-ED25519\x00"
HEADER = struct.Struct("<4sHH12I8s")                     # 64 bytes, same layout as sapp_sign.py


def canonical(v):
    return json.dumps(v, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def openssl(*args, data=None):
    r = subprocess.run(["openssl", *args], input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if r.returncode:
        raise RuntimeError(r.stderr.decode(errors="replace"))
    return r.stdout


def pub_pem(priv):
    return openssl("pkey", "-in", str(priv), "-pubout")


def key_id(pem):
    return hashlib.sha256(openssl("pkey", "-pubin", "-outform", "DER", data=pem)).hexdigest()


def raw_pub(pem):
    der = openssl("pkey", "-pubin", "-outform", "DER", data=pem)
    return der[-32:]


def sign(priv, data, d):
    src, sig = Path(d) / "m.bin", Path(d) / "s.bin"
    src.write_bytes(data)
    openssl("pkeyutl", "-sign", "-rawin", "-inkey", str(priv), "-in", str(src), "-out", str(sig))
    return sig.read_bytes()


def new_key(d, name):
    p = Path(d) / f"{name}.pem"
    openssl("genpkey", "-algorithm", "ED25519", "-out", str(p))
    return p


def iso(dt):
    return dt.isoformat()


def issue_cert(d, root_priv, pub_priv, subject="Test Publisher", not_before=None, not_after=None,
               usage=("codeSigning",), issuer_key_id=None, tweak=None):
    now = datetime.now(timezone.utc)
    pem = pub_pem(pub_priv)
    cert = {"format": "SAND-PUBLISHER-CERT", "version": 1, "publisherId": "org.test.publisher", "subject": subject,
            "publicKeyPem": pem.decode("ascii"), "keyId": key_id(pem),
            "issuerKeyId": issuer_key_id or key_id(pub_pem(root_priv)),
            "notBefore": iso(not_before or now - timedelta(days=1)), "notAfter": iso(not_after or now + timedelta(days=365)),
            "keyUsage": list(usage)}
    if tweak:
        tweak(cert)
    cert["issuerSignature"] = base64.b64encode(sign(root_priv, canonical(cert), d)).decode("ascii")
    return cert


def sign_sapp(d, v1, cert, pub_priv, tweak_block=None):
    fields = list(HEADER.unpack_from(v1))
    assert fields[0] == b"SAPP" and fields[1] == 1 and not fields[11] and not fields[12]
    block = {"format": "SAND-SAPP-SIGNATURE", "version": 1, "algorithm": "Ed25519",
             "publisherCertificate": cert, "signature": base64.b64encode(bytes(64)).decode("ascii")}
    if tweak_block:
        tweak_block(block)
    n = len(canonical(block))
    fields[1] = 2
    fields[10] = len(v1) + n
    fields[11] = len(v1)
    fields[12] = n
    header = HEADER.pack(*fields)
    block["signature"] = base64.b64encode(sign(pub_priv, DOMAIN + header + v1[64:], d)).decode("ascii")
    out = canonical(block)
    assert len(out) == n
    return header + v1[64:] + out


def trust_bin(root_pems, revoked=(), flags=()):
    """Binary trust blob read by the host CLI and embedded by the kernel self-test."""
    b = struct.pack("<I", len(root_pems))
    for i, pem in enumerate(root_pems):
        b += bytes.fromhex(key_id(pem)) + raw_pub(pem) + struct.pack("<I", flags[i] if i < len(flags) else 0)
    b += struct.pack("<I", len(revoked))
    for r in revoked:
        b += bytes.fromhex(r)
    return b


def build_kernel_fixture(outdir, v1_path):
    """Used by the Makefile: writes signed-hello.sapp and trust.bin (root pubkey/id, publisher id)."""
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as d:
        root, pub = new_key(d, "root"), new_key(d, "pub")
        cert = issue_cert(d, root, pub)
        (out / "signed.sapp").write_bytes(sign_sapp(d, Path(v1_path).read_bytes(), cert, pub))
        # layout: root key id (32) | root public key (32) | publisher key id (32)
        (out / "stci-fixture.bin").write_bytes(bytes.fromhex(key_id(pub_pem(root))) + raw_pub(pub_pem(root)) + bytes.fromhex(key_id(pub_pem(pub))))


if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "kernel":
        build_kernel_fixture(sys.argv[2], sys.argv[3])
    else:
        print("usage: stci_fixtures.py kernel <outdir> <unsigned-v1.sapp>")
        sys.exit(2)
