#!/usr/bin/env python3
"""BIFROST (native AHCI driver) in QEMU: a SATA disk behind an ICH9 AHCI controller is found, read (host-seeded
sector 0), written and read back through DMA, and the self-test restores the sector it used."""
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(HERE, "..", "..", "build", "sand-os.img"))
SECTORS = 2048
LAST_ORIG = bytes((i * 3 + 1) & 0xFF for i in range(512))


def make_disk(path):
    with open(path, "wb") as f:
        f.write(b"BIFROST-SEED".ljust(512, b"\0"))
        f.write(b"\0" * 512 * (SECTORS - 2))
        f.write(LAST_ORIG)


class BifrostTest(unittest.TestCase):
    def boot(self, tmp, ahci):
        img = os.path.join(tmp, "os.img")
        shutil.copyfile(IMAGE, img)
        vm = SandVM(img, ahci_disks=ahci).start()
        self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 60), vm.debug_log()[-500:])
        return vm

    def test_ahci_disk_read_write_dma(self):
        with tempfile.TemporaryDirectory() as tmp:
            disk = os.path.join(tmp, "sata.img")
            make_disk(disk)
            vm = self.boot(tmp, [disk])
            try:
                log = vm.debug_log()
                self.assertIn("SAND_TEST:M13_BIFROST_PASS", log)
                self.assertIn("SAND_TEST:M13_BIFROST_SEED_READ", log)       # the host's sector 0 arrived intact via DMA
                self.assertIn("SAND_TEST:M6_MIMIR_FORMAT_PASS", log)        # the ATA boot disk and MIMIR are unaffected
            finally:
                vm.stop()
            data = open(disk, "rb").read()
            self.assertEqual(data[-512:], LAST_ORIG)                          # pattern written, read back, then restored
            self.assertTrue(data.startswith(b"BIFROST-SEED"))

    def test_no_controller_is_not_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            vm = self.boot(tmp, [])
            try:
                self.assertIn("SAND_TEST:M13_BIFROST_NONE", vm.debug_log())
            finally:
                vm.stop()


if __name__ == "__main__":
    unittest.main()
