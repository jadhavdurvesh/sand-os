#!/usr/bin/env python3
"""Unit tests for tools/gen_app_table.py (manifest-driven launcher catalogue)."""
import json, subprocess, sys, tempfile, unittest
from pathlib import Path

GEN = Path(__file__).resolve().parents[2] / "tools/gen_app_table.py"


def manifest(**launcher):
    m = {"id": "org.test.x", "name": "X", "version": "1.0.0"}
    if launcher is not None:
        m["launcher"] = launcher
    return m


class GenTest(unittest.TestCase):
    def run_gen(self, apps):
        with tempfile.TemporaryDirectory() as d:
            for name, m in apps.items():
                (Path(d) / name).mkdir()
                (Path(d) / name / "manifest.json").write_text(json.dumps(m))
            out = Path(d) / "out.c"
            r = subprocess.run([sys.executable, str(GEN), str(out), d, *apps], capture_output=True, text=True)
            return r.returncode, (out.read_text() if out.exists() else ""), r.stderr

    good = dict(label="Alpha", subtitle="First", accent="#112233", accent2="#445566", glyph="A", order=5)

    def test_ordering_and_hidden_apps(self):
        plain = {"id": "org.test.p", "name": "P", "version": "1"}
        rc, c, _ = self.run_gen({"zeta": manifest(**dict(self.good, label="Zeta", order=1)), "alpha": manifest(**self.good), "plain": plain})
        self.assertEqual(rc, 0)
        self.assertLess(c.index('"Zeta"'), c.index('"Alpha"'))          # sorted by launcher.order
        self.assertNotIn('"plain", "', c.split("launchers[]")[1])       # apps without a launcher object are not listed
        self.assertIn("#define LAUNCHER_COUNT 2u", c)

    def test_rejects_bad_manifests(self):
        for bad in (dict(self.good, accent="red"), dict(self.good, accent2="#12345"), dict(self.good, glyph="AB"), dict(self.good, glyph=" "),
                    dict(self.good, label=""), dict(self.good, label="x" * 16), dict(self.good, subtitle="y" * 24), dict(self.good, order=-1),
                    dict(self.good, order=True), dict(self.good, label="café")):
            rc, _, err = self.run_gen({"app": manifest(**bad)})
            self.assertNotEqual(rc, 0, bad); self.assertIn("gen_app_table", err)

    def test_rejects_missing_fields_and_bad_names(self):
        rc, _, _ = self.run_gen({"app": {"name": "X"}}); self.assertNotEqual(rc, 0)
        rc, _, _ = self.run_gen({"Bad-Name": manifest(**self.good)}); self.assertNotEqual(rc, 0)

    def test_launcher_limit(self):
        apps = {f"a{i}": manifest(**dict(self.good, label=f"A{i}")) for i in range(10)}
        rc, _, _ = self.run_gen(apps); self.assertNotEqual(rc, 0)

    def test_quote_and_backslash_glyphs_are_escaped(self):
        rc, c, _ = self.run_gen({"q": manifest(**dict(self.good, glyph="'")), "b": manifest(**dict(self.good, label="B", glyph="\\"))})
        self.assertEqual(rc, 0); self.assertIn("'\\''", c); self.assertIn("'\\\\'", c)


if __name__ == "__main__":
    unittest.main()
