#!/usr/bin/env python3
"""DAWN boot splash in QEMU: the splash is shown first, the iris reveal finishes before the desktop's first frame,
and the final screen is the desktop (the splash leaves nothing behind)."""
import os
import shutil
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sand_qemu import SandVM  # noqa: E402

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(HERE, "..", "..", "build", "sand-os.img"))


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


class DawnTest(unittest.TestCase):
    def test_splash_then_reveal_then_desktop(self):
        with tempfile.TemporaryDirectory() as tmp:
            img = os.path.join(tmp, "os.img")
            shutil.copyfile(IMAGE, img)
            vm = SandVM(img).start()
            try:
                self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 60), vm.debug_log()[-400:])
                log = vm.debug_log()
                order = [log.index(m) for m in ("SAND_TEST:DAWN_SPLASH_SHOWN", "SAND_TEST:M6_MIMIR_FORMAT_PASS",
                                                "SAND_TEST:DAWN_REVEAL_DONE", "SAND_TEST:M11_DESKTOP_FIRST_FRAME")]
                self.assertEqual(order, sorted(order))                                  # splash -> boot work -> reveal -> desktop
                time.sleep(1.0)
                shot = os.path.join(tmp, "final.ppm")
                vm.screendump(shot)
                w, h, px = read_ppm(shot)
                def rgb(x, y):
                    i = (y * w + x) * 3
                    return px[i], px[i + 1], px[i + 2]
                # the desktop's top bar is the first thing under the iris: its text is bright on a dark strip, and the splash's
                # progress-bar label ("desktop") must be gone from where it was drawn
                bar = sum(sum(rgb(x, 10)) for x in range(0, w, 8)) / (w // 8)
                self.assertLess(bar, 3 * 90)                                             # dark top bar, not the lit horizon
                # the splash label sat at x = w/2, y = 93% + 24; the dock now covers that row, but its gaps between tiles are plain
                # glass, so a leftover glyph would show there as bright pixels against the gap's own colour higher up
                ref = rgb(512, 700)
                stray = [rgb(x, y) for x in range(507, 518, 2) for y in range(726, 746, 3)]
                self.assertTrue(all(abs(c[i] - ref[i]) < 45 for c in stray for i in range(3)), stray[:4])
            finally:
                vm.stop()


if __name__ == "__main__":
    unittest.main()
