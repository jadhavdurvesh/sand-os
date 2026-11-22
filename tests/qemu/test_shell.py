import time
#!/usr/bin/env python3
"""M9 acceptance: drive the native ring-3 shell over the serial console."""
import os
import shutil
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
        for _ in range(12):                     # disk apps and m6-test.txt come first; walk down to alpha.txt
            self.vm.send("j")
            if self.vm.expect(r"first-file-body", 2, start=mark) is not None:
                break
        self.assertIsNotNone(self.vm.expect(r"first-file-body", 5, start=mark), self.vm.serial_text[mark:][-400:])
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

    def test_11_apps_load_from_mimir(self):
        """jail and lumen are not in the kernel image: they live in MIMIR (multi-sector files) and are
        loaded on demand, with their permissions taken from the manifest inside the image."""
        apps = self.cmd("apps")
        for name in ("hello", "jail", "lumen"):
            self.assertIn(name, apps)
        ls = self.cmd("ls")
        self.assertRegex(ls, r"jail\.sapp.*?\d{4,} bytes")  # a multi-sector file (more than 512 bytes)
        out = self.cmd("run jail", timeout=20)
        self.assertIn("jail: win=-5 set=-5 fsr=-5 fsw=-5 spawn=-5 read=-5 ipc=-5 get=0", out)   # manifest said console.write only
        self.assertIn("cannot start", self.cmd("run nosuchapp"))
        self.assertIn("exited with status 0", out)

    def _install(self, name, data, timeout=8):
        """Drive the shell's install command: hex lines, one acknowledged ('.') at a time, then 'end'."""
        mark = len(self.vm.serial_text)
        self.vm.send("install %s\r" % name)
        self.assertIsNotNone(self.vm.expect(r"send hex lines", 10, start=mark), self.vm.serial_text[mark:][-300:])
        for i in range(0, len(data), 100):
            mark = len(self.vm.serial_text)
            self.vm.send(data[i:i + 100].hex() + "\r")
            self.assertIsNotNone(self.vm.expect(r"\.", timeout, start=mark) or self.vm.expect(r"install: ", 1, start=mark), "no ack")
        mark = len(self.vm.serial_text)
        self.vm.send("end\r")
        self.vm.expect(r"(installed|install: )[^\r\n]*[\r\n]+(sand >)?", timeout, start=mark)
        return self.vm.serial_text[mark:]

    def test_12_install_an_app_from_the_shell(self):
        """Install a SAPP image at run time: it is stored in MIMIR, then runs with its manifest permissions."""
        import pathlib
        image = pathlib.Path("build/apps/hello.sapp").read_bytes()
        out = self._install("greeter", image)
        self.assertIn("installed", out, out[-300:])
        self.assertIn("greeter", self.cmd("apps"))
        self.assertIn("greeter.sapp", self.cmd("ls"))
        run = self.cmd("run greeter", timeout=20)
        self.assertIn("Hello from a native SAND application", run)
        self.cmd("rm greeter.sapp")
        self.assertNotIn("greeter", self.cmd("apps"))

    def test_13_installer_refuses_bad_input(self):
        import pathlib
        image = pathlib.Path("build/apps/hello.sapp").read_bytes()
        self.assertIn("refused", self._install("locus", image))               # would shadow a built-in app
        self.assertIn("refused", self._install("junk", b"NOTASAPP" * 40))     # not a SAPP image
        self.assertIn("refused", self._install("Bad-Name", image))            # invalid name
        ls = self.cmd("ls")
        for name in ("locus.sapp", "junk.sapp"):
            self.assertNotIn(name, ls)

    def test_10_permissions_are_enforced(self):
        """An app that declares only console.write is refused every gated call (SAND_E_PERM = -5)."""
        out = self.cmd("run jail", timeout=20)
        self.assertIn("jail: win=-5 set=-5 fsr=-5 fsw=-5 spawn=-5 read=-5 ipc=-5 get=0", out)
        self.assertIn("exited with status 0", out)
        self.assertNotIn("jail.txt", self.cmd("ls"))                 # the refused write really did not happen
        self.assertIn("accent=0", self.cmd("settings"))              # nor did the settings write

    def test_09_system_settings_and_persistence(self):
        """ABI v5 settings: validated by the kernel, readable back, persisted to system.cfg on MIMIR."""
        out = self.vm.run_command("settings")
        self.assertRegex(out, r"accent=0\s+motion=0\s+clock24=1")
        self.vm.run_command("settings accent 2")
        self.vm.run_command("settings clock24 0")
        self.assertRegex(self.vm.run_command("settings"), r"accent=2\s+motion=0\s+clock24=0")
        self.assertIn("rejected", self.vm.run_command("settings accent 9"))           # out of range
        self.assertIn("rejected", self.vm.run_command("settings motion 2"))
        self.assertRegex(self.vm.run_command("settings"), r"accent=2")                 # unchanged by rejected writes
        time.sleep(1.5)                                                                # debounced write to system.cfg
        self.assertIn("accent=2", self.vm.run_command("cat system.cfg"))
        # a fresh boot from a copy of the disk restores the saved settings
        copy = os.path.join(self.vm.dir, "persist.img"); shutil.copyfile(IMAGE, copy)
        vm2 = SandVM(copy).start()
        try:
            self.assertTrue(vm2.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40), vm2.debug_log()[-400:])
            vm2.expect(r"type 'help'", 10)
            self.assertRegex(vm2.run_command("settings"), r"accent=2\s+motion=0\s+clock24=0")
        finally:
            vm2.stop()
        self.vm.run_command("settings accent 0"); self.vm.run_command("settings clock24 1")
        time.sleep(1.2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
