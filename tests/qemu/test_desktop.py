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

    def test_06_multiple_app_windows_and_calculator(self):
        """Background apps: several windows at once, keyboard goes to the focused one."""
        self.vm.run_command("open chronos")
        self.vm.run_command("open quanta")
        time.sleep(2.0)
        before = self.shot("desktop-apps-before.ppm")
        for key in ("7", "asterisk", "6", "equal"):
            self.vm.sendkey(key)
        time.sleep(1.0)
        after = self.shot("desktop-apps.ppm")
        # QUANTA's display panel (surface 12,12 -> window at 168,118 + 36 title) changes after 7*6.
        region = [(x, y) for x in range(330, 450) for y in range(190, 230)]
        diff = sum(1 for (x, y) in region if pixel(before, x, y) != pixel(after, x, y))
        self.assertGreater(diff, 60)
        self.vm.sendkey("q")                                  # quanta (focused) exits
        time.sleep(0.8)
        self.vm.sendkey("q")                                  # then chronos
        time.sleep(1.5)
        self.vm.run_command("ps")                             # prompt reaps the finished background apps
        ps = self.vm.run_command("ps")
        self.assertNotIn("quanta", ps)
        self.assertNotIn("chronos", ps)

    def move_to(self, x, y):
        """Pointer starts wherever earlier tests left it; steer relative to the tracked position."""
        cx, cy = getattr(self, "_ptr", (None, None))
        if cx is None:
            for _ in range(12):                      # clamp to the top-left corner first (deltas are per-packet limited)
                self.vm.mouse_move(-100, -100)
            cx, cy = 0, 0
        dx, dy = x - cx, y - cy
        while dx or dy:
            sx = max(-100, min(100, dx)); sy = max(-100, min(100, dy))
            self.vm.mouse_move(sx, sy); dx -= sx; dy -= sy
        DesktopTest._ptr = (x, y)

    def test_07_dock_launcher_starts_an_app(self):
        """Click the launcher tile, pick LOCUS from the panel: the app starts and opens a window."""
        self.move_to(444, 716)                               # launcher tile in the dock
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        panel = self.shot("desktop-launcher.ppm")
        self.assertGreater(sum(1 for x in range(420, 680) for y in range(420, 640) if sum(pixel(panel, x, y)) > 500), 200)   # panel text and icons
        self.move_to(501, 465)                               # LOCUS row (panel now lists four apps)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(2.5)
        win = self.shot("desktop-launched.ppm")
        ps = self.vm.run_command("ps")
        self.assertIn("locus", ps)
        self.vm.sendkey("q")                                  # focused app exits; detached child is reaped by the kernel
        time.sleep(1.5)
        ps = self.vm.run_command("ps")
        self.assertNotIn("locus", ps)

    def test_08_resize_and_pixel_buffer(self):
        """PRISM owns a pixel buffer; dragging its dune-grip resizes the window and the app redraws."""
        self.vm.run_command("open prism")
        time.sleep(2.0)
        before = self.shot("desktop-prism-a.ppm")
        # client area (130,122)-(550,422) is filled by app-owned, upscaled pixels (many distinct colours)
        colours = {pixel(before, x, y) for x in range(150, 530, 7) for y in range(140, 400, 7)}
        self.assertGreater(len(colours), 40)
        self.assertEqual(pixel(before, 600, 300), pixel(self.shot("desktop-prism-a2.ppm"), 600, 300))   # outside is wallpaper
        self.move_to(540, 412)                               # grip at the bottom-right corner
        self.vm.mouse_button(1)
        for _ in range(8):
            self.vm.mouse_move(10, 5)
        self.vm.mouse_button(0)
        time.sleep(1.5)
        after = self.shot("desktop-prism-b.ppm")
        self.move_to(620, 450)
        grown = sum(1 for x in range(560, 600) for y in range(160, 400, 3) if pixel(before, x, y) != pixel(after, x, y))
        self.assertGreater(grown, 150)                       # the window now covers area that used to be wallpaper
        colours = {pixel(after, x, y) for x in range(560, 600, 3) for y in range(160, 400, 7)}
        self.assertGreater(len(colours), 10)                 # ...and the app redrew it at the new size
        self.vm.sendkey("q")
        time.sleep(1.5)
        self.vm.run_command("ps")
        self.assertNotIn("prism", self.vm.run_command("ps"))
        gone = self.shot("desktop-prism-closed.ppm")
        self.assertEqual(pixel(gone, 300, 300), pixel(self.shot("desktop-prism-closed2.ppm"), 300, 300))


if __name__ == "__main__":
    unittest.main()
