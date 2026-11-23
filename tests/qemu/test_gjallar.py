#!/usr/bin/env python3
"""GJALLAR boot score in QEMU: an Intel HDA controller records to a WAV file (QEMU's wav audio backend).
The captured audio must be the score itself (sample-exact start, no dropouts through boot), the splash must be paced
by it (the reveal lands on the first big hit), a key press must skip the ceremony, and the score must fade for the user."""
import os
import shutil
import sys
import tempfile
import time
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from sand_qemu import SandVM  # noqa: E402

try:
    import numpy as np
    import gjallar_pack  # noqa: E402
except ImportError:      # numpy is only needed to compare the captured audio with the score
    np = None

IMAGE = os.environ.get("SAND_IMAGE", os.path.join(ROOT, "build", "sand-os.img"))
SCORE = os.path.join(ROOT, "assets", "audio", "gjallarhorn.gjlr")


def reference_mono(seconds):
    """What the kernel decoder must produce for the first `seconds` of the score (software volume 200/256 applied)."""
    data = open(SCORE, "rb").read()
    blocks = int(seconds * 44100 / gjallar_pack.BLOCK_FRAMES) + 1
    parts = [gjallar_pack.decode_block(data[512 + b * gjallar_pack.BLOCK_BYTES:512 + (b + 1) * gjallar_pack.BLOCK_BYTES]) for b in range(blocks)]
    return np.concatenate(parts).astype(np.float64).mean(axis=1) * 200.0 / 256.0


def read_wav(path):
    w = wave.open(path)
    assert w.getframerate() == 44100 and w.getnchannels() == 2
    return np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, 2).astype(np.float64).mean(axis=1)


class GjallarTest(unittest.TestCase):
    def boot(self, tmp, wav):
        img = os.path.join(tmp, "os.img")
        shutil.copyfile(IMAGE, img)
        return SandVM(img, audio_wav=wav).start()

    def test_01_score_plays_paces_the_reveal_and_fades_on_input(self):
        with tempfile.TemporaryDirectory() as tmp:
            wav = os.path.join(tmp, "out.wav")
            vm = self.boot(tmp, wav)
            try:
                self.assertTrue(vm.wait_marker("SAND_TEST:GJALLAR_PLAYING", 60), vm.debug_log()[-500:])
                t0 = time.time()
                self.assertTrue(vm.wait_marker("SAND_TEST:DAWN_REVEAL_DONE", 60), vm.debug_log()[-500:])
                held = time.time() - t0
                self.assertGreater(held, 9.0)                                  # the reveal waited for the score's first hit (about 11.9 s in)
                self.assertLess(held, 14.0)
                self.assertTrue(vm.wait_marker("SAND_TEST:M11_DESKTOP_FIRST_FRAME", 20))
                time.sleep(2.0)
                vm.sendkey("spc")                                              # input: the score gives way
                time.sleep(3.0)
            finally:
                vm.stop()
            self.assertTrue(os.path.getsize(wav) > 44)
            if np is None:
                self.skipTest("numpy not available: captured audio not compared")
            got = read_wav(wav)
            self.assertLess(len(got) / 44100.0, 25.0)                          # it stopped long before the 62 s score ended
            ref = reference_mono(12.0)
            for start_s in (1.0, 5.0, 9.0):                                    # same samples at the same offsets: sample-exact start, no dropouts
                a = int(start_s * 44100); seg = got[a:a + 44100]; r = ref[a:a + 44100]
                self.assertGreater(np.corrcoef(seg, r)[0, 1], 0.999, f"mismatch at {start_s}s")
            tail = got[-2205:]
            self.assertLess(float(np.sqrt((tail ** 2).mean())), 300.0)         # faded to silence before the stream stopped

    def test_02_key_press_skips_the_ceremony(self):
        with tempfile.TemporaryDirectory() as tmp:
            vm = self.boot(tmp, os.path.join(tmp, "out.wav"))
            try:
                self.assertTrue(vm.wait_marker("SAND_TEST:GJALLAR_PLAYING", 60))
                time.sleep(0.5)
                vm.sendkey("spc")
                t0 = time.time()
                self.assertTrue(vm.wait_marker("SAND_TEST:DAWN_REVEAL_DONE", 30), vm.debug_log()[-500:])
                self.assertLess(time.time() - t0, 6.0)                          # no 12 s wait
                self.assertTrue(vm.wait_marker("SAND_TEST:ALL_PASS", 30))
            finally:
                vm.stop()

    def test_03_no_sound_hardware_means_no_waiting(self):
        with tempfile.TemporaryDirectory() as tmp:
            img = os.path.join(tmp, "os.img")
            shutil.copyfile(IMAGE, img)
            vm = SandVM(img).start()
            try:
                self.assertTrue(vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 60))
                self.assertIn("SAND_TEST:GJALLAR_NONE", vm.debug_log())
            finally:
                vm.stop()


if __name__ == "__main__":
    unittest.main()
