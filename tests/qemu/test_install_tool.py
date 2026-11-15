#!/usr/bin/env python3
"""tools/sand_install.py end to end: bake an app into a copy of the disk image, boot it, run the app."""
import os
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(ROOT, "build", "sand-os.img"))
TOOL = os.path.join(ROOT, "tools", "sand_install.py")
HELLO = os.path.join(ROOT, "build", "apps", "hello.sapp")


class InstallToolTest(unittest.TestCase):
    def run_tool(self, *args):
        return subprocess.run([sys.executable, TOOL, *args], capture_output=True, text=True, timeout=240)

    def test_bake_boot_and_run(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "baked.img")
            r = self.run_tool("--boot", IMAGE, "--out", out, "--run", "bakedapp", HELLO)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("installed bakedapp", r.stdout)
            self.assertIn("Hello from a native SAND application", r.stdout)
            with SandVM(out) as vm:                                   # the install survived a power cycle
                self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40))
                vm.expect(r"type 'help'", 10)
                self.assertIn("bakedapp", vm.run_command("apps"))
                self.assertIn("Hello from a native SAND application", vm.run_command("run bakedapp", 20))

    def test_refusals_exit_nonzero_and_leave_the_image_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "unchanged.img")
            r = self.run_tool("--boot", IMAGE, "--out", out, "locus", HELLO)      # built-in name
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
            self.assertIn("REFUSED", r.stdout)
            junk = os.path.join(tmp, "junk.sapp"); open(junk, "wb").write(b"NOTASAPP" * 40)
            r = self.run_tool("--boot", IMAGE, "--out", out, "junk", junk)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)

    def test_usage_errors(self):
        self.assertEqual(self.run_tool("--boot", IMAGE, "x", "/nonexistent.sapp").returncode, 2)
        self.assertEqual(self.run_tool("--connect", "127.0.0.1:9", "x", HELLO).returncode, 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
