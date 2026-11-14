#!/usr/bin/env python3
"""Host matrix for the native SAPP v2 verifier (kernel/security/stci.c): signature, certificate chain,
validity, revocation, policy and malformed-input handling. Builds the verifier with ASan/UBSan."""
import os, subprocess, sys, tempfile, shutil
from datetime import datetime, timedelta, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import stci_fixtures as F  # noqa: E402

OK, ARG, FMT, UNSUP, UNTRUSTED, CERT, EXPIRED, REVOKED, SIG, POLICY = 0, -1, -2, -3, -4, -5, -6, -7, -8, -9
NOW = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
failures = 0


def check(name, got, want):
    global failures
    ok = (got != OK) if want == "fail" else (got == want)
    if not ok:
        failures += 1
        print(f"FAIL {name}: got {got}, want {want}")


def main():
    v1_path = Path(os.environ.get("SAND_TEST_V1", ROOT / "build/apps/hello.sapp"))
    if not v1_path.exists():
        print(f"missing {v1_path}; run `make apps` first"); return 2
    v1 = v1_path.read_bytes()
    d = Path(tempfile.mkdtemp(prefix="stci-test-"))
    try:
        cli = d / "cli"
        subprocess.check_call(["cc", "-std=c11", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-Wall", "-Wextra", "-Werror",
                               "-I", str(ROOT / "include"), "-I", str(ROOT / "kernel/crypto"), "-o", str(cli), str(ROOT / "tests/veyra/stci_verify_cli.c"),
                               str(ROOT / "kernel/security/stci.c"), str(ROOT / "kernel/crypto/ed25519.c"), str(ROOT / "kernel/crypto/sha512.c"), str(ROOT / "kernel/crypto/sha256.c")])
        root, other_root, pub, pub2 = (F.new_key(d, n) for n in ("root", "other", "pub", "pub2"))
        rpem, opem, ppem = F.pub_pem(root), F.pub_pem(other_root), F.pub_pem(pub)
        trust = d / "trust.bin"; trust.write_bytes(F.trust_bin([rpem]))

        def run(image, trust_bytes=None, now=None, flags=0, name="img"):
            now = now or datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S")
            p = d / name; p.write_bytes(image)
            t = d / "t.bin"; t.write_bytes(trust_bytes if trust_bytes is not None else trust.read_bytes())
            r = subprocess.run([str(cli), str(p), str(t), now, str(flags)], capture_output=True, text=True)
            if r.returncode != 0:
                return f"crash/exit {r.returncode}: {r.stderr[-200:]}"
            return int(r.stdout.strip())

        cert = F.issue_cert(d, root, pub)
        good = F.sign_sapp(d, v1, cert, pub)
        check("valid image verifies", run(good), OK)
        check("v1 image is not 'signed'", run(v1), UNSUP)

        # --- payload / header / metadata / signature tampering ---
        hdr = F.HEADER.unpack_from(good)
        sig_off = hdr[11]
        positions = [64, 70, 100, sig_off - 1, sig_off // 2] + [20, 16, 40 + 4]
        for pos in positions:
            for bit in (0, 7):
                b = bytearray(good); b[pos] ^= 1 << bit
                check(f"bit flip @{pos}/{bit} rejected", run(bytes(b)), "fail")
        for pos in range(sig_off, len(good), 37):
            b = bytearray(good); b[pos] ^= 1
            check(f"signature-block flip @{pos} rejected", run(bytes(b)), "fail")
        # every single-byte change of the executable bytes must be rejected (sampled densely)
        for pos in range(0, sig_off, 11):
            b = bytearray(good); b[pos] ^= 0x01
            check(f"byte flip @{pos}", run(bytes(b)), "fail")

        # --- truncation / extension ---
        for n in list(range(0, 200, 7)) + [len(good) - 1, len(good) - 88, sig_off, sig_off + 1]:
            check(f"truncated to {n}", run(good[:n]), "fail")
        check("trailing garbage", run(good + b"\0"), FMT)
        check("empty image", run(b""), FMT)

        # --- chain / trust ---
        check("empty trust store", run(good, F.trust_bin([])), UNTRUSTED)
        check("different root provisioned", run(good, F.trust_bin([opem])), UNTRUSTED)
        check("trusted among several", run(good, F.trust_bin([opem, rpem])), OK)
        forged = F.issue_cert(d, other_root, pub, issuer_key_id=F.key_id(rpem))      # claims the trusted root, signed by another key
        check("forged issuer signature", run(F.sign_sapp(d, v1, forged, pub)), CERT)
        check("cert field changed after issuance", run(F.sign_sapp(d, v1, dict(cert, subject="Evil Corp"), pub)), CERT)
        check("cert key swapped", run(F.sign_sapp(d, v1, dict(cert, publicKeyPem=F.pub_pem(pub2).decode()), pub2)), CERT)
        wrong_signer = F.sign_sapp(d, v1, cert, pub2)
        check("image signed by a different key than the certificate", run(wrong_signer), SIG)

        # --- key usage / algorithm / format ---
        check("wrong key usage", run(F.sign_sapp(d, v1, F.issue_cert(d, root, pub, usage=("timestamping",)), pub)), POLICY)
        check("extra key usage", run(F.sign_sapp(d, v1, F.issue_cert(d, root, pub, usage=("codeSigning", "x")), pub)), POLICY)
        def alg(b): b["algorithm"] = "RSA"
        check("unknown algorithm", run(F.sign_sapp(d, v1, cert, pub, tweak_block=alg)), UNSUP)
        def ver(b): b["version"] = 2
        check("unknown signature-block version", run(F.sign_sapp(d, v1, cert, pub, tweak_block=ver)), UNSUP)
        def fmt(b): b["format"] = "X"
        check("wrong block format", run(F.sign_sapp(d, v1, cert, pub, tweak_block=fmt)), FMT)
        def extra(b): b["zz"] = 1
        check("unsigned-extra member still verifies only if canonical+signed", run(F.sign_sapp(d, v1, cert, pub, tweak_block=extra)), OK)
        tz = F.issue_cert(d, root, pub, not_before=datetime.now(timezone(timedelta(hours=5, minutes=30))) - timedelta(days=1))
        check("non-UTC timestamp offset fails closed", run(F.sign_sapp(d, v1, tz, pub)), CERT)

        # --- validity window / clock ---
        old = F.issue_cert(d, root, pub, not_before=datetime.now(timezone.utc) - timedelta(days=30), not_after=datetime.now(timezone.utc) - timedelta(days=1))
        check("expired certificate", run(F.sign_sapp(d, v1, old, pub)), EXPIRED)
        future = F.issue_cert(d, root, pub, not_before=datetime.now(timezone.utc) + timedelta(days=1), not_after=datetime.now(timezone.utc) + timedelta(days=30))
        check("not yet valid certificate", run(F.sign_sapp(d, v1, future, pub)), EXPIRED)
        check("no clock fails closed", run(good, now="-"), EXPIRED)
        check("garbage clock fails closed", run(good, now="not-a-time-stamp!!"), EXPIRED)
        check("clock in the past of notBefore", run(good, now="2001-01-01T00:00:00"), EXPIRED)

        # --- revocation ---
        check("revoked publisher key", run(good, F.trust_bin([rpem], revoked=[F.key_id(ppem)])), REVOKED)
        check("unrelated revocation ignored", run(good, F.trust_bin([rpem], revoked=["00" * 32])), OK)
        check("revoked root", run(good, F.trust_bin([rpem], flags=[1])), REVOKED)
        check("attestation required but unavailable", run(good, flags=4), UNSUP)

        # --- JSON strictness: non-canonical blocks are rejected ---
        ws = bytearray(good); i = good.rindex(b'{"algorithm"'); ws[i:i] = b" "
        check("whitespace before block shifts layout", run(bytes(ws)), "fail")
        blk = good[sig_off:]
        import json
        pretty = json.dumps(json.loads(blk), indent=1).encode()
        h = list(F.HEADER.unpack_from(good)); h[10] = sig_off + len(pretty); h[12] = len(pretty)
        check("pretty-printed (non-canonical) block", run(F.HEADER.pack(*h) + good[64:sig_off] + pretty), "fail")
        unsorted = json.dumps(json.loads(blk), separators=(",", ":"), sort_keys=False).encode()
        if unsorted != blk:
            h[10] = sig_off + len(unsorted); h[12] = len(unsorted)
            check("unsorted keys", run(F.HEADER.pack(*h) + good[64:sig_off] + unsorted), "fail")

        # --- interoperability with the real sand-apps tools when available ---
        apps = Path(os.environ.get("SAND_APPS_DIR", ROOT.parent / "sand-apps"))
        if (apps / "tools/sandcert.py").exists() and (apps / "tools/sapp_sign.py").exists():
            t = d / "interop"; t.mkdir()
            sh = lambda *a: subprocess.run([sys.executable, str(apps / "tools" / a[0]), *a[1:]], check=True, capture_output=True, text=True)
            sh("sandcert.py", "authority-init", "--name", "Interop Root", "--output", str(t / "auth"))
            sh("sandcert.py", "publisher-issue", "--authority", str(t / "auth/authority.json"), "--root-key", str(t / "auth/root-private.pem"),
               "--publisher-id", "org.interop", "--subject", "Interop", "--output", str(t / "pub"))
            sh("sapp_sign.py", str(v1_path), "-o", str(t / "signed.sapp"), "--certificate", str(t / "pub/publisher-cert.json"), "--private-key", str(t / "pub/publisher-private.pem"))
            tb = F.trust_bin([(t / "auth/root-public.pem").read_bytes()])
            check("image signed by the real sand-apps tools verifies natively", run((t / "signed.sapp").read_bytes(), tb), OK)
            print("interop with sand-apps tools: checked")
        else:
            print("sand-apps tools not found; interop check skipped")
    finally:
        shutil.rmtree(d, ignore_errors=True)
    if failures:
        print(f"{failures} failure(s)"); return 1
    print("stci chain matrix passed"); return 0


if __name__ == "__main__":
    sys.exit(main())
