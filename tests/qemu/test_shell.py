#!/usr/bin/env python3
"""M9 acceptance: drive the native ring-3 shell over the serial console."""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(os.path.dirname(__file__), "..", "..", "build", "sand-os.img"))


class ShellTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vm = SandVM(IMAGE).start()
        assert cls.vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40), cls.vm.debug_log()[-600:]
        assert cls.vm.expect(r"type 'help'", 10), cls.vm.serial_text[-400:]

    @classmethod
    def tearDownClass(cls):
        cls.vm.stop()

    def cmd(self, text, timeout=10):
        return self.vm.run_command(text, timeout)

    def test_01_help_and_echo(self):
        self.assertIn("write <file> <text>", self.cmd("help"))
        self.assertIn("hello  sand", self.cmd("echo hello  sand"))

    def test_02_files_roundtrip(self):
        self.assertIn("saved 18 bytes", self.cmd("write note.txt SAND shell is real"))
        self.assertIn("SAND shell is real", self.cmd("cat note.txt"))
        self.assertIn("note.txt", self.cmd("ls"))
        self.assertIn("removed", self.cmd("rm note.txt"))
        self.assertIn("no such file", self.cmd("cat note.txt"))

    def test_03_system_commands(self):
        ps = self.cmd("ps")
        self.assertIn("sandsh", ps)
        self.assertIn("running", ps)
        self.assertIn("KiB", self.cmd("mem"))
        self.assertIn("min", self.cmd("uptime"))
        devices = self.cmd("devices")
        for name in ("serial0", "kbd0", "mouse0", "ata0", "pit0"):
            self.assertIn(name, devices)
        self.assertIn("hello", self.cmd("apps"))

    def test_04_run_apps(self):
        self.assertIn("Hello from a native SAND application", self.cmd("run hello"))
        self.assertIn("exited with status 0", self.cmd("run hello"))
        out = self.cmd("run abitest", timeout=20)
        self.assertIn("ABITEST:PASS", out)

    def test_05_fault_is_contained(self):
        out = self.cmd("run crash")
        self.assertIn("killed by CPU fault, vector 14", out)
        self.assertIn("alive-after-crash", self.cmd("echo alive-after-crash"))

    def test_06_line_editing(self):
        # "echo wrongX" then two backspaces erase "gX"; typing "ok" yields "wronok".
        mark = len(self.vm.serial_text)
        self.vm.send("echo wrongX\x08\x08ok\r")
        self.assertIsNotNone(self.vm.expect(r"\nwronok", 6, start=mark), self.vm.serial_text[mark:])
        self.assertIn("unknown command", self.cmd("frobnicate"))

    def test_07_keyboard_irq_reaches_shell(self):
        """A real PS/2 key press (injected via the QEMU monitor) must reach the shell."""
        mark = len(self.vm.serial_text)
        for key in ("e", "c", "h", "o", "spc", "k", "b", "d", "ret"):
            self.vm.sendkey(key)
        m = self.vm.expect(r"kbd", 8, start=mark)
        self.assertIsNotNone(m, self.vm.serial_text[mark:])
        self.assertIn("SAND_TEST:M8_KEYBOARD_IRQ_PASS", self.vm.debug_log())

    def test_08_locus_file_explorer(self):
        """Native LOCUS (from the sand-apps catalogue): list, create, preview, delete, quit."""
        self.cmd("write alpha.txt first-file-body")
        mark = len(self.vm.serial_text)
        self.vm.send("run locusc\r")
        self.assertIsNotNone(self.vm.expect(r"LOCUS.*alpha\.txt", 10, start=mark), self.vm.serial_text[mark:][-400:])
        mark = len(self.vm.serial_text)
        self.vm.send("j")                      # m6-test.txt is first; move to alpha.txt
        self.assertIsNotNone(self.vm.expect(r"first-file-body", 10, start=mark), self.vm.serial_text[mark:][-400:])
        mark = len(self.vm.serial_text)
        self.vm.send("nbeta.txt\rsecond body\r")
        self.assertIsNotNone(self.vm.expect(r"beta\.txt.*second body", 10, start=mark), self.vm.serial_text[mark:][-400:])
        mark = len(self.vm.serial_text)
        self.vm.send("dy")
        self.assertIsNotNone(self.vm.expect(r"alpha\.txt", 10, start=mark))
        self.vm.send("q")
        self.assertIsNotNone(self.vm.expect(r"exited with status 0", 10, start=mark), self.vm.serial_text[mark:][-300:])
        ls = self.cmd("ls")
        self.assertIn("alpha.txt", ls)
        self.assertNotIn("beta.txt", ls)
        self.cmd("rm alpha.txt")


if __name__ == "__main__":
    unittest.main(verbosity=2)
