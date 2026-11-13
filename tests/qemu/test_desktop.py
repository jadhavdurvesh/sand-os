#!/usr/bin/env python3
"""M11 acceptance: the SAND Dusk desktop boots, draws, follows the pointer and mirrors the shell."""
import os
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(os.path.dirname(__file__), "..", "..", "build", "sand-os.img"))
SHOT = os.environ.get("SAND_SHOT_DIR", os.path.join(os.path.dirname(__file__), "..", "..", "build"))


def load_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def pixel(img, x, y):
    w, _, px = img
    i = (y * w + x) * 3
    return tuple(px[i:i + 3])


class DesktopTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.vm = SandVM(IMAGE).start()
        assert cls.vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 40), cls.vm.debug_log()[-600:]
        cls.vm.expect(r"type 'help'", 10)

    @classmethod
    def tearDownClass(cls):
        cls.vm.stop()

    def shot(self, name):
        path = os.path.join(SHOT, name)
        self.vm.screendump(path)
        for _ in range(20):
            if os.path.exists(path) and os.path.getsize(path) > 1000:
                break
            time.sleep(0.1)
        return load_ppm(path)

    def test_01_markers(self):
        log = self.vm.debug_log()
        self.assertIn("SAND_TEST:M11_DESKTOP_STARTED", log)
        self.assertIn("SAND_TEST:M11_DESKTOP_FIRST_FRAME", log)

    def test_02_frame_has_chrome_and_wallpaper(self):
        time.sleep(1.0)
        img = self.shot("desktop-boot.ppm")
        self.assertEqual(img[:2], (1024, 768))
        sky_top, horizon = pixel(img, 500, 40), pixel(img, 990, 480)
        self.assertLess(sum(sky_top), 200)          # deep indigo sky
        self.assertGreater(horizon[0], sky_top[0] + 40)   # warm glow near the horizon
        bar = pixel(img, 600, 10)
        self.assertLess(sum(bar), 160)              # dark menu bar
        dock = pixel(img, 512, 735)
        self.assertNotEqual(dock, pixel(img, 100, 735))   # floating dock is drawn

    def test_03_pointer_moves_and_is_drawn(self):
        before = self.shot("desktop-ptr-a.ppm")
        for _ in range(10):
            self.vm.mouse_move(12, 8)
        time.sleep(1.0)
        self.assertIn("SAND_TEST:M11_POINTER_EVENT", self.vm.debug_log())
        after = self.shot("desktop-ptr-b.ppm")
        self.assertNotEqual(before[2], after[2])

    def test_04_console_window_mirrors_shell(self):
        self.vm.send("echo SANDDESKTOPOK\r")
        time.sleep(1.0)
        img = self.shot("desktop-echo.ppm")
        # The word is drawn in the console window: look for bright text pixels on the dark console surface.
        bright = 0
        for y in range(120, 300):
            for x in range(56, 400):
                if sum(pixel(img, x, y)) > 600:
                    bright += 1
        self.assertGreater(bright, 150)
        self.assertTrue(self.vm.expect(r"SANDDESKTOPOK", 5), self.vm.serial_text[-300:])

    def test_05_window_service_locus(self):
        """An app window created through the window service is drawn, takes keys, and closes."""
        self.vm.send("run locus\r")
        time.sleep(2.5)
        img = self.shot("desktop-locus.ppm")
        r, g, b = pixel(img, 200, 402)                       # LOCUS "New file" button (jade gradient)
        self.assertGreater(g, r + 40, (r, g, b))
        before = self.vm.serial_text
        self.vm.sendkey("n")                                 # keyboard goes to the focused app window
        time.sleep(1.0)
        self.vm.sendkey("q")                                 # app exits, window disappears, shell returns
        self.assertIsNotNone(self.vm.expect(r"exited with status 0", 10, start=len(before)), self.vm.serial_text[-300:])
        time.sleep(1.0)
        gone = self.shot("desktop-locus-closed.ppm")
        self.assertNotEqual(pixel(gone, 200, 402), pixel(img, 200, 402))
        out = self.vm.run_command("ls")
        self.assertIn("note-1.txt", out)
        self.vm.run_command("rm note-1.txt")


if __name__ == "__main__":
    unittest.main()
