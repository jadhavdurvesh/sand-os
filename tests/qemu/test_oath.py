#!/usr/bin/env python3
"""OATH acceptance in QEMU: the signed boot profile provisions the launch gate (require-signed + one trust root).

Images (built by `make signed-images`): build/sand-os-signed.img (valid profile, v2-signed disk apps) and
build/sand-os-badoath.img (same, but the profile's signature is damaged -> fail closed)."""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
from sand_qemu import SandVM  # noqa: E402

BUILD = os.path.join(ROOT, "build")
SIGNED = os.path.join(BUILD, "sand-os-signed.img")
BADOATH = os.path.join(BUILD, "sand-os-badoath.img")
PLAIN = os.environ.get("SAND_IMAGE", os.path.join(BUILD, "sand-os.img"))
HELLO = os.path.join(BUILD, "apps", "hello.sapp")
OATH_DIR = os.path.join(BUILD, "oath")
INSTALL = os.path.join(ROOT, "tools", "sand_install.py")
SIGN = os.path.join(ROOT, "tools", "sapp_sign.py")


def boot(image):
    vm = SandVM(image).start()
    assert vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 60), vm.debug_log()[-600:]
    assert vm.expect(r"type 'help'", 10)
    return vm


def install_tool(*args):
    return subprocess.run([sys.executable, INSTALL, *args], capture_output=True, text=True, timeout=300)


class OathTest(unittest.TestCase):
    def test_01_signed_profile_runs_signed_apps_and_builtins(self):
        with tempfile.TemporaryDirectory() as tmp:
            img = os.path.join(tmp, "o.img")
            shutil.copyfile(SIGNED, img)
            vm = boot(img)
            try:
                log = vm.debug_log()
                self.assertIn("SAND_AUDIT:OATH_LOADED policy=require-signed roots=1", log)
                self.assertIn("sigil valid", vm.run_command("mimir seal"))                                  # the profile is part of the signed factory manifest
                self.assertRegex(vm.run_command("mimir sigil"), r"boot\.oath\s+sealed")
                self.assertIn("exited with status 0", vm.run_command("run jail", 20))                       # v2-signed disk app, trusted root
                self.assertRegex(vm.debug_log(), r"SAND_AUDIT:LAUNCH_SIGNED pub=[0-9a-f]{8} r=[0-9a-f]{8} jail")
                self.assertIn("ABITEST:PASS", vm.run_command("run abitest", 30))                            # built-in apps need no signature
                self.assertIn("SAND_AUDIT:LAUNCH_BUILTIN", vm.debug_log())
            finally:
                vm.stop()

    def test_02_unsigned_and_foreign_signed_installs_are_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            r = install_tool("--boot", SIGNED, "--out", os.path.join(tmp, "x.img"), "plainapp", HELLO)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)                                           # unsigned v1: LAUNCH_DENIED_UNSIGNED
            foreign = os.path.join(tmp, "foreign")
            subprocess.run([sys.executable, SIGN, "init", foreign], check=True)                              # another publisher, another root
            bad = os.path.join(tmp, "foreign.sapp")
            subprocess.run([sys.executable, SIGN, "sign", foreign, HELLO, bad], check=True)
            r = install_tool("--boot", SIGNED, "--out", os.path.join(tmp, "y.img"), "foreignapp", bad)
            self.assertEqual(r.returncode, 1, r.stdout + r.stderr)                                           # signed, but the root is not trusted
            good = os.path.join(tmp, "good.sapp")
            subprocess.run([sys.executable, SIGN, "sign", OATH_DIR, HELLO, good], check=True)
            out = os.path.join(tmp, "z.img")
            r = install_tool("--boot", SIGNED, "--out", out, "--run", "goodapp", good)                        # signed by the trusted root: installed and runs
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertIn("Hello from a native SAND application", r.stdout)

    def test_03_damaged_profile_fails_closed(self):
        with tempfile.TemporaryDirectory() as tmp:
            img = os.path.join(tmp, "b.img")
            shutil.copyfile(BADOATH, img)
            vm = boot(img)
            try:
                self.assertIn("SAND_AUDIT:OATH_INVALID", vm.debug_log())
                self.assertIn("cannot start", vm.run_command("run jail", 20))                                # even a properly signed app: no roots
                self.assertIn("ABITEST:PASS", vm.run_command("run abitest", 30))                              # the kernel's own apps still run
                self.assertRegex(vm.debug_log(), r"LAUNCH_DENIED_SIGNATURE status=-4 jail")
            finally:
                vm.stop()

    def test_04_default_image_keeps_the_developer_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            img = os.path.join(tmp, "d.img")
            shutil.copyfile(PLAIN, img)
            vm = boot(img)
            try:
                self.assertIn("SAND_AUDIT:OATH_ABSENT", vm.debug_log())
                self.assertIn("exited with status 0", vm.run_command("run jail", 20))                        # unsigned disk app still allowed (audited)
                self.assertIn("LAUNCH_UNSIGNED_DEVELOPER jail", vm.debug_log())
            finally:
                vm.stop()


if __name__ == "__main__":
    unittest.main()
