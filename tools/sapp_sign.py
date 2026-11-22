#!/usr/bin/env python3
"""Sign SAPP v1 apps into SAPP v2 with a throw-away developer publisher (STCI format, see tools/stci_fixtures.py).

  sapp_sign.py init <dir>                  create root.pem, pub.pem and a 10-year publisher certificate (idempotent)
  sapp_sign.py sign <dir> <in.sapp> <out.sapp>

The root public key is what a boot profile (boot.oath, tools/mkmimir.py --oath) lists as a trust root. Developer keys only: a real
publisher keeps its private keys offline and gets its certificate from a root SAND provisions."""
import json
import sys
import tempfile
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import stci_fixtures as fx  # noqa: E402


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "init":
        d = Path(sys.argv[2])
        d.mkdir(parents=True, exist_ok=True)
        if (d / "cert.json").exists():
            return
        root, pub = fx.new_key(d, "root"), fx.new_key(d, "pub")
        cert = fx.issue_cert(d, root, pub, subject="SAND developer publisher", not_after=datetime.now(timezone.utc) + timedelta(days=3650))
        (d / "cert.json").write_bytes(fx.canonical(cert))
    elif len(sys.argv) == 5 and sys.argv[1] == "sign":
        d = Path(sys.argv[2])
        cert = json.loads((d / "cert.json").read_text())
        with tempfile.TemporaryDirectory() as tmp:
            Path(sys.argv[4]).write_bytes(fx.sign_sapp(tmp, Path(sys.argv[3]).read_bytes(), cert, d / "pub.pem"))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
