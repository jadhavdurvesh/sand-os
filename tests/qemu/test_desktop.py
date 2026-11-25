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
        self.assertIn("SAND_TEST:ICONS_LOADED", log)

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

    def test_03b_desktop_starts_empty_and_start_menu_opens_the_console(self):
        """No window is open after boot; the SAND (Start) button lists the apps and opens the System Console."""
        DesktopTest._ptr = (None, None)
        before = self.shot("desktop-empty.ppm")
        self.assertLess(sum(pixel(before, 200, 200)), 400)                    # no console window where it used to open (dark sky, no glass panel)
        self.move_to(40, 12)                                                    # the SAND button
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        menu = self.shot("desktop-start.ppm")
        self.assertGreater(sum(1 for x in range(20, 280) for y in range(60, 540) if sum(pixel(menu, x, y)) > 500), 300)   # the Start panel's labels
        self.move_to(130, 463)                                                  # "System Console" row (eighth)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        win = self.shot("desktop-console-opened.ppm")
        self.assertNotEqual(before[2], win[2])
        self.assertGreater(sum(1 for x in range(56, 400) for y in range(70, 100) if sum(pixel(win, x, y)) > 300), 30)    # the window's title bar text

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
        DesktopTest._ptr = (None, None)                      # re-home: clamp to the corner, then steer from a known position
        self.move_to(410, 716)                               # launcher tile in the dock
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        panel = self.shot("desktop-launcher.ppm")
        self.assertGreater(sum(1 for x in range(380, 640) for y in range(110, 640) if sum(pixel(panel, x, y)) > 500), 200)   # panel text and icons
        self.move_to(500, 257)                              # LOCUS row (third: CITADEL, CAIRN, LOCUS, QUANTA, CHRONOS, KEYSTONE, LUMEN, then the two system rows)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(2.5)
        win = self.shot("desktop-launched.ppm")
        ps = self.vm.run_command("ps")
        self.assertIn("locus", ps)
        self.vm.sendkey("q")                                  # focused app exits; detached child is reaped by the kernel
        time.sleep(1.5)
        ps = self.vm.run_command("ps")
        self.assertNotIn("locus", ps)

    def test_07a_task_manager_lists_and_ends_an_app(self):
        """The Task Manager (third system row) lists running apps; its End button stops one for real and is audited."""
        DesktopTest._ptr = (None, None)
        self.move_to(410, 716)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        self.move_to(500, 257)                              # LOCUS
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(2.5)
        self.assertIn("locus", self.vm.run_command("ps"))
        self.move_to(376, 716)                              # the dock grew a tile for the running app, so the launcher moved left
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        self.move_to(500, 621)                              # Task Manager row (tenth)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.5)
        shot = self.shot("desktop-taskmgr.ppm")
        r, g, b = pixel(shot, 730, 318)                     # inside the first row's End button (left of its label): coral
        self.assertGreater(r, g + 20)
        self.move_to(752, 318)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(2.0)
        self.assertNotIn("locus", self.vm.run_command("ps"))
        self.assertIn("SAND_AUDIT:TASK_ENDED locus", self.vm.debug_log())
        after = self.shot("desktop-taskmgr-after.ppm")
        r, g, b = pixel(after, 730, 318)
        self.assertLess(r, g + 20)                          # the row is gone; no End button left there
        self.move_to(382, 101)                              # the window's close dot
        self.vm.mouse_button(1); self.vm.mouse_button(0)

    def test_07b_disk_app_in_the_launcher(self):
        """LUMEN lives only in MIMIR; its manifest puts it in the launcher and the dock starts it from disk."""
        DesktopTest._ptr = (None, None)
        self.move_to(410, 716)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        self.move_to(500, 465)                              # seventh row: LUMEN (disk app, listed after the built-in apps)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(2.5)
        self.shot("desktop-disk-launch.ppm")
        self.assertIn("lumen", self.vm.run_command("ps"))
        self.vm.sendkey("q")
        time.sleep(1.5)
        self.assertNotIn("lumen", self.vm.run_command("ps"))

    def test_08_resize_and_pixel_buffer(self):
        """LUMEN owns a pixel buffer; dragging its dune-grip resizes the window and the app redraws."""
        self.vm.run_command("open lumen")
        time.sleep(2.0)
        before = self.shot("desktop-lumen-a.ppm")
        # client area (130,122)-(550,422) is filled by app-owned, upscaled pixels (many distinct colours)
        colours = {pixel(before, x, y) for x in range(150, 530, 7) for y in range(140, 400, 7)}
        self.assertGreater(len(colours), 40)
        self.assertEqual(pixel(before, 600, 300), pixel(self.shot("desktop-lumen-a2.ppm"), 600, 300))   # outside is wallpaper
        self.move_to(540, 412)                               # grip at the bottom-right corner
        self.vm.mouse_button(1)
        for _ in range(8):
            self.vm.mouse_move(10, 5)
        self.vm.mouse_button(0)
        time.sleep(1.5)
        after = self.shot("desktop-lumen-b.ppm")
        self.move_to(620, 450)
        grown = sum(1 for x in range(560, 600) for y in range(160, 400, 3) if pixel(before, x, y) != pixel(after, x, y))
        self.assertGreater(grown, 150)                       # the window now covers area that used to be wallpaper
        colours = {pixel(after, x, y) for x in range(560, 600, 3) for y in range(160, 400, 7)}
        self.assertGreater(len(colours), 10)                 # ...and the app redrew it at the new size
        self.vm.sendkey("q")
        time.sleep(1.5)
        self.vm.run_command("ps")
        self.assertNotIn("lumen", self.vm.run_command("ps"))
        gone = self.shot("desktop-lumen-closed.ppm")
        self.assertEqual(pixel(gone, 300, 300), pixel(self.shot("desktop-lumen-closed2.ppm"), 300, 300))

    def type_keys(self, text):
        names = {" ": "spc", "\n": "ret", "-": "minus", ".": "dot"}
        for ch in text:
            self.vm.sendkey(names.get(ch, ch))

    def test_09_cairn_terminal_window(self):
        """CAIRN is a real window: keys typed into it run shell commands whose output it renders."""
        self.vm.run_command("open cairn")
        time.sleep(2.0)
        before = self.shot("desktop-cairn-a.ppm")
        self.type_keys("help\n")
        time.sleep(1.5)
        after = self.shot("desktop-cairn-b.ppm")
        diff = sum(1 for x in range(150, 640, 2) for y in range(130, 480, 2) if pixel(before, x, y) != pixel(after, x, y))
        self.assertGreater(diff, 400)                         # command list rendered in the window
        self.type_keys("ps\n")
        time.sleep(1.0)
        self.type_keys("exit\n")                              # the shell ends and the window closes
        time.sleep(2.0)
        self.vm.run_command("ps")
        self.assertNotIn("cairn", self.vm.run_command("ps"))

    def test_09b_citadel_assistant(self):
        """CITADEL: an offline assistant window. Settings changes are asked back (y/n) before they run; it opens apps."""
        self.vm.run_command("open citadel")
        time.sleep(2.5)
        before = self.shot("desktop-citadel-a.ppm")
        self.type_keys("what time is it\n")
        time.sleep(1.0)
        after = self.shot("desktop-citadel-b.ppm")
        diff = sum(1 for x in range(150, 640, 2) for y in range(150, 480, 2) if pixel(before, x, y) != pixel(after, x, y))
        self.assertGreater(diff, 300)                                      # the question and the answer were drawn
        self.assertIn("accent=0", self.vm.run_command("settings"))
        self.type_keys("set accent jade\n")                               # needs confirmation: nothing changes yet
        time.sleep(1.0)
        self.assertIn("accent=0", self.vm.run_command("settings"))
        self.type_keys("n\n")                                             # declined
        time.sleep(1.0)
        self.assertIn("accent=0", self.vm.run_command("settings"))
        self.type_keys("set accent jade\n")
        time.sleep(0.8)
        self.type_keys("y\n")                                             # confirmed: the kernel setting really changes
        time.sleep(1.2)
        self.assertIn("accent=3", self.vm.run_command("settings"))
        self.type_keys("set accent champagne\n")
        time.sleep(0.8)
        self.type_keys("y\n")
        time.sleep(1.2)
        self.assertIn("accent=0", self.vm.run_command("settings"))
        self.type_keys("exit\n")
        time.sleep(2.0)
        self.assertNotRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+citadel")

    def test_09c_citadel_notes_and_reminders(self):
        """CITADEL notes persist in notes.txt on MIMIR; a reminder fires inside the window; clearing notes asks first."""
        self.vm.run_command("rm notes.txt")
        self.vm.run_command("open citadel")
        time.sleep(2.5)
        self.type_keys("note buy milk\n")
        time.sleep(0.8)
        self.type_keys("note call mum\n")
        time.sleep(0.8)
        self.assertIn("buy milk", self.vm.run_command("cat notes.txt"))
        self.assertIn("call mum", self.vm.run_command("cat notes.txt"))
        before = self.shot("desktop-citadel-notes-a.ppm")
        self.type_keys("remind me in 3 seconds to stretch\n")
        time.sleep(1.0)
        quiet = self.shot("desktop-citadel-notes-b.ppm")
        time.sleep(4.5)                                                   # the reminder fires and the input ring turns bright
        fired = self.shot("desktop-citadel-notes-c.ppm")
        diff = sum(1 for x in range(150, 640, 2) for y in range(150, 480, 2) if pixel(quiet, x, y) != pixel(fired, x, y))
        self.assertGreater(diff, 100)
        self.type_keys("clear notes\n")                                  # asks first: nothing is deleted yet
        time.sleep(0.8)
        self.assertIn("buy milk", self.vm.run_command("cat notes.txt"))
        self.type_keys("y\n")
        time.sleep(1.0)
        self.assertNotIn("buy milk", self.vm.run_command("cat notes.txt"))
        self.type_keys("exit\n")
        time.sleep(2.0)
        self.assertNotRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+citadel")

    def test_09d_herald_toast_outlives_the_window(self):
        """HERALD: a reminder set in CITADEL survives closing the window; the background service raises a desktop toast."""
        self.vm.run_command("open citadel")
        time.sleep(2.5)
        self.type_keys("remind me in 12 seconds to stretch\n")
        time.sleep(1.0)
        self.assertRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+herald")   # the service was started on demand
        self.type_keys("exit\n")
        time.sleep(2.0)
        self.assertNotRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+citadel")
        self.assertRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+herald")   # ...and keeps running without the window
        base = self.shot("desktop-herald-a.ppm")
        def toast_diff(img):                                           # the card sits under the menu bar, top right
            return sum(1 for x in range(680, 1000, 2) for y in range(46, 100, 2) if pixel(base, x, y) != pixel(img, x, y))
        shown = None
        for _ in range(14):
            time.sleep(1.0)
            img = self.shot("desktop-herald-b.ppm")
            if toast_diff(img) > 300:
                shown = img; break
        self.assertIsNotNone(shown, "no toast appeared")
        for _ in range(12):                                            # and it goes away again by itself
            time.sleep(1.0)
            if toast_diff(self.shot("desktop-herald-c.ppm")) < 50:
                break
        else:
            self.fail("toast never disappeared")

    def test_10_keystone_changes_the_system(self):
        """KEYSTONE writes real system settings: choosing Jade recolours the desktop accent itself."""
        self.vm.run_command("open keystone")
        time.sleep(2.0)
        before = self.shot("desktop-keystone-a.ppm")
        self.assertEqual(pixel(before, 17, 17), (0xF2, 0xC9, 0x8A))                      # menu-bar sun: champagne
        DesktopTest._ptr = (None, None)                                                  # earlier tests moved the pointer by raw deltas; re-home it
        self.move_to(404, 272)                                                           # the Jade swatch (surface 274,150)
        self.vm.mouse_button(1); self.vm.mouse_button(0)
        time.sleep(1.0)
        after = self.shot("desktop-keystone-b.ppm")
        r, g, b = pixel(after, 17, 17)
        self.assertLess(abs(r - 0x74) + abs(g - 0xD6) + abs(b - 0xAA), 12)               # menu-bar sun is now jade
        self.assertIn("accent=3", self.vm.run_command("settings"))
        self.vm.sendkey("m")                                                             # keyboard: reduced motion on
        time.sleep(0.5)
        self.assertIn("motion=1", self.vm.run_command("settings"))
        self.vm.sendkey("1"); self.vm.sendkey("m")                                       # restore defaults
        time.sleep(0.5)
        self.assertRegex(self.vm.run_command("settings"), r"accent=0\s+motion=0")
        self.vm.sendkey("q")
        time.sleep(1.5)
        self.assertNotRegex(self.vm.run_command("ps"), r"(running|ready|blocked)\s+keystone")   # exited is fine

    def bright(self, img, x0, y0, x1, y1):
        return sum(1 for x in range(x0, x1, 2) for y in range(y0, y1, 2) if sum(pixel(img, x, y)) > 200)   # the sky is darker than any icon pixel

    def changed(self, a, b, x0, y0, x1, y1):
        return sum(1 for x in range(x0, x1, 2) for y in range(y0, y1, 2)
                   if max(abs(p - q) for p, q in zip(pixel(a, x, y), pixel(b, x, y))) > 30)

    def click(self):
        self.vm.mouse_button(1); self.vm.mouse_button(0)

    def test_03a_desktop_shortcuts(self):
        """Shortcuts on the wallpaper: double-click opens, drag rearranges, right-click removes, the Start menu adds."""
        DesktopTest._ptr = (None, None)
        base = self.shot("desktop-shortcuts.ppm")
        self.assertGreater(self.bright(base, 40, 56, 92, 110), 20)              # LOCUS icon in the first cell
        self.assertGreater(self.bright(base, 40, 156, 92, 210), 20)             # CITADEL in the second
        self.move_to(66, 84)                                                     # double-click LOCUS
        self.vm.double_click()
        time.sleep(2.5)
        self.assertIn("locus", self.vm.run_command("ps"))
        self.vm.sendkey("q"); time.sleep(1.5)
        self.assertNotIn("locus", self.vm.run_command("ps"))
        self.move_to(66, 185)                                                    # drag CITADEL to the middle of the desktop
        self.vm.mouse_button(1)
        self.move_to(200, 300); self.move_to(400, 420)
        self.vm.mouse_button(0); time.sleep(0.8)
        moved = self.shot("desktop-shortcuts-dragged.ppm")
        self.assertLess(self.bright(moved, 40, 156, 92, 210), 5)                # its old cell is empty
        self.assertGreater(self.changed(moved, base, 316, 356, 356, 396), 100)   # and it sits in the cell nearest the drop point
        self.move_to(334, 383)                                                   # right-click it: Open / Remove from Desktop
        self.vm.mouse_button(2); self.vm.mouse_button(0); time.sleep(0.8)
        menu = self.shot("desktop-shortcuts-menu.ppm")
        self.assertGreater(self.changed(menu, moved, 350, 400, 540, 470), 200)   # the menu is drawn
        self.move_to(400, 442)
        self.click(); time.sleep(0.8)
        gone = self.shot("desktop-shortcuts-removed.ppm")
        self.assertLess(self.changed(gone, base, 316, 356, 356, 396), 5)         # nothing left in that cell
        self.move_to(410, 716)                                                   # Start/launcher panel: right-click CITADEL, Add to Desktop
        self.click(); time.sleep(1.0)
        self.move_to(500, 145)
        self.vm.mouse_button(2); self.vm.mouse_button(0); time.sleep(0.8)
        self.move_to(550, 204)
        self.click(); time.sleep(1.0)
        back = self.shot("desktop-shortcuts-added.ppm")
        self.assertGreater(self.bright(back, 40, 156, 92, 210), 20)             # CITADEL is back in the first free cell


    def test_03aa_keyboard_rubber_band_and_show_desktop(self):
        """Arrow keys move the selection, Delete removes it, a rubber band selects, Show Desktop hides every window."""
        self.vm.sendkey("esc"); time.sleep(0.4)
        a = self.shot("desktop-kbd-a.ppm")
        self.vm.sendkey("down"); time.sleep(0.6)                                 # nothing focused yet: the first shortcut is selected
        b = self.shot("desktop-kbd-b.ppm")
        self.assertGreater(self.changed(a, b, 30, 50, 100, 116), 50)
        self.vm.sendkey("down"); time.sleep(0.6)                                 # next cell down
        c = self.shot("desktop-kbd-c.ppm")
        self.assertGreater(self.changed(c, b, 30, 150, 100, 216), 50)
        self.assertLess(self.changed(c, a, 30, 50, 100, 116), 30)                 # the first one lost its highlight
        self.vm.sendkey("delete"); time.sleep(0.6)
        d = self.shot("desktop-kbd-d.ppm")
        self.assertLess(self.bright(d, 40, 156, 92, 210), 5)                    # the selected shortcut is gone
        self.assertGreater(self.bright(d, 40, 56, 92, 110), 20)                 # the other stays
        DesktopTest._ptr = (None, None)
        self.move_to(150, 66)                                                    # rubber band from empty wallpaper around the first icon
        self.vm.mouse_button(1)
        self.move_to(90, 90); self.move_to(26, 120)
        self.vm.mouse_button(0); time.sleep(0.6)
        e = self.shot("desktop-band.ppm")
        self.assertGreater(self.changed(e, d, 30, 56, 100, 116), 50)             # selected by the band
        self.vm.sendkey("delete"); time.sleep(0.6)
        f = self.shot("desktop-band-deleted.ppm")
        self.assertLess(self.bright(f, 40, 56, 92, 110), 5)
        self.move_to(600, 300)                                                   # wallpaper menu: System Console (row 5), then Show Desktop (row 3)
        self.vm.mouse_button(2); self.vm.mouse_button(0); time.sleep(0.6)
        self.move_to(650, 300 + 8 + 4 * 34 + 17)
        self.click(); time.sleep(1.5)
        g = self.shot("desktop-console-open.ppm")
        self.assertGreater(self.changed(g, f, 200, 200, 500, 400), 500)
        self.move_to(900, 300)
        self.vm.mouse_button(2); self.vm.mouse_button(0); time.sleep(0.6)
        self.move_to(950 - 100, 300 + 8 + 2 * 34 + 17)
        self.click(); time.sleep(1.5)
        h = self.shot("desktop-shown.ppm")
        self.assertLess(self.changed(h, f, 200, 200, 500, 400), 30)              # every window is hidden again


if __name__ == "__main__":
    unittest.main()
