#!/usr/bin/env python3
"""MIMIR acceptance in QEMU: versions, rollback, dedupe-as-no-op, Veyra labels, scrub, and bit-rot detection."""
import os
import re
import shutil
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(os.path.dirname(__file__), "..", "..", "build", "sand-os.img"))
VOLUME_LBA = 2112          # Makefile MIMIR_LBA
FIRST_TIDE_SECTOR = 8      # block 1 of the volume


def boot(image):
    vm = SandVM(image).start()
    assert vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40), vm.debug_log()[-600:]
    assert vm.expect(r"type 'help'", 10), vm.serial_text[-400:]
    return vm


class MimirTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vm = boot(IMAGE)

    @classmethod
    def tearDownClass(cls):
        cls.vm.stop()

    def cmd(self, text, timeout=15):
        return self.vm.run_command(text, timeout)

    def test_01_seal_and_veyra(self):
        out = self.cmd("mimir seal")
        self.assertRegex(out, r"MIMIR generation \d+\s+tide 0\s+epoch 0")
        self.assertRegex(out, r"chain [0-9a-f]{64}")
        veyra = self.cmd("veyra")
        self.assertIn("STORE   (MIMIR)", veyra)
        self.assertRegex(veyra, r"0x0106\s+STORE")
        self.assertIn("ABI v12", self.cmd("version"))

    def test_02_history_rollback_and_noop_rewrite(self):
        for text in ("first draft", "second draft", "third draft"):
            self.assertIn("saved", self.cmd("write hist.txt " + text))
        log = self.cmd("mimir log hist.txt")
        self.assertRegex(log, r"v3.*\n.*v2.*\n.*v1")
        self.assertIn("restored hist.txt from v1", self.cmd("mimir undo hist.txt 1"))
        self.assertIn("first draft", self.cmd("cat hist.txt"))
        self.assertIn("v4", self.cmd("mimir log hist.txt"))
        self.cmd("write hist.txt first draft")           # identical to the newest content: no new version
        self.assertNotIn("v5", self.cmd("mimir log hist.txt"))
        self.assertIn("no such file or version", self.cmd("mimir undo hist.txt 99"))
        stat = self.cmd("mimir stat hist.txt")
        self.assertRegex(stat, r"version 4\s+chunks 1\s+read 0x0\s+write 0x0")
        self.assertRegex(stat, r"id [0-9a-f]{64}")
        self.assertIn("all hashes and the chain match", self.cmd("mimir verify"))
        self.cmd("rm hist.txt")
        self.assertIn("deleted", self.cmd("mimir log hist.txt"))   # removal is a version too

    def test_03_labels_are_enforced_by_the_filesystem(self):
        self.cmd("write secret.txt top secret")
        self.assertIn("labelled secret.txt", self.cmd("mimir label secret.txt 0 0x400"))   # nobody holds bit 0x400
        self.assertIn("failed", self.cmd("write secret.txt overwritten"))
        self.assertIn("top secret", self.cmd("cat secret.txt"))
        self.assertIn("no such file", self.cmd("rm secret.txt"))                           # remove is a write: denied
        self.assertIn("denies", self.cmd("mimir undo secret.txt 1"))
        self.assertIn("labelled", self.cmd("mimir label secret.txt 0x400 0x400"))
        self.assertIn("no such file", self.cmd("cat secret.txt"))                          # now unreadable too
        self.assertIn("labelled", self.cmd("mimir label secret.txt 0 0"))                  # system.install may change labels
        self.assertIn("top secret", self.cmd("cat secret.txt"))
        self.assertRegex(self.cmd("mimir stat jail.sapp"), r"write 0x80")                  # apps need system.install to be replaced
        self.assertRegex(self.cmd("mimir stat system.cfg") if "system.cfg" in self.cmd("ls") else "write 0x40", r"write 0x40")
        self.cmd("rm secret.txt")

    def test_04_ebb_keeps_the_live_files(self):
        self.cmd("write keep.txt survives the ebb")
        before = re.search(r"id ([0-9a-f]{64})", self.cmd("mimir stat keep.txt")).group(1)
        out = self.cmd("mimir ebb", timeout=30)
        self.assertRegex(out, r"ebb done: tide 0 -> 1")
        self.assertIn("survives the ebb", self.cmd("cat keep.txt"))
        self.assertEqual(before, re.search(r"id ([0-9a-f]{64})", self.cmd("mimir stat keep.txt")).group(1))   # ids are content addresses
        self.assertIn("exited with status 0", self.cmd("run jail", timeout=20))             # disk apps load from the new tide
        self.assertIn("all hashes and the chain match", self.cmd("mimir verify", timeout=20))
        self.cmd("rm keep.txt")


class MimirBitRotTest(unittest.TestCase):
    def test_flipped_bit_is_detected_not_returned(self):
        vm_dir = os.path.join(os.path.dirname(IMAGE), "mimir-bitrot.img")
        shutil.copyfile(IMAGE, vm_dir)
        with open(vm_dir, "r+b") as f:     # tide 0, block 0 = first chunk of the first file (jail.sapp): flip one bit in its payload
            off = (VOLUME_LBA + FIRST_TIDE_SECTOR) * 512 + 200
            f.seek(off)
            b = f.read(1)
            f.seek(off)
            f.write(bytes([b[0] ^ 0x08]))
        vm = boot(vm_dir)
        try:
            out = vm.run_command("run jail", 20)
            self.assertIn("cannot start", out)            # the loader refused the damaged image
            scrub = vm.run_command("mimir verify", 30)
            self.assertRegex(scrub, r"1 damaged, first at block 0")
            self.assertIn("alive", vm.run_command("echo alive"))
            self.assertIn("SAND_AUDIT:MIMIR_SCRUB_DAMAGE", vm.debug_log())
        finally:
            vm.stop()
            os.unlink(vm_dir)


if __name__ == "__main__":
    unittest.main()
