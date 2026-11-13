"""Helpers to boot a SAND OS image in QEMU and drive it from the host.

QEMU is only an external test fixture. Interaction channels:
  * debug console (port 0xE9)  -> SAND_TEST:* markers written by the kernel
  * COM1 serial (tcp)          -> the SAND console; we type into the shell here
  * QEMU monitor (unix socket) -> sendkey / mouse events / screendump
"""
import os
import re
import socket
import subprocess
import tempfile
import time


class SandVM:
    def __init__(self, image, extra_disks=(), memory="128M", workdir=None):
        self.image = image
        self.extra_disks = list(extra_disks)
        self.memory = memory
        self.dir = workdir or tempfile.mkdtemp(prefix="sandvm-")
        self.debug_path = os.path.join(self.dir, "debug.log")
        self.monitor_path = os.path.join(self.dir, "monitor.sock")
        self.proc = None
        self.serial = None
        self.serial_text = ""
        self.mon = None

    # -- lifecycle ---------------------------------------------------------
    def start(self):
        for path in (self.debug_path, self.monitor_path):
            if os.path.exists(path):
                os.unlink(path)
        s = socket.socket(); s.bind(("127.0.0.1", 0)); port = s.getsockname()[1]; s.close()
        cmd = ["qemu-system-i386", "-machine", "pc", "-m", self.memory,
               "-drive", f"file={self.image},format=raw,if=ide,index=0"]
        for i, disk in enumerate(self.extra_disks, start=1):
            cmd += ["-drive", f"file={disk},format=raw,if=ide,index={i}"]
        cmd += ["-display", "none", "-no-reboot",
                "-debugcon", f"file:{self.debug_path}", "-global", "isa-debugcon.iobase=0xe9",
                "-serial", f"tcp:127.0.0.1:{port},server=on,wait=off",
                "-monitor", f"unix:{self.monitor_path},server,nowait"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 10
        while time.time() < deadline:
            try:
                self.serial = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if self.serial is None:
            raise RuntimeError("could not connect to QEMU serial")
        self.serial.setblocking(False)
        while time.time() < deadline and not os.path.exists(self.monitor_path):
            time.sleep(0.1)
        self.mon = socket.socket(socket.AF_UNIX); self.mon.settimeout(3)
        self.mon.connect(self.monitor_path)
        time.sleep(0.2); self._drain_monitor()
        return self

    def stop(self):
        try:
            if self.mon:
                self.mon.sendall(b"quit\n")
        except OSError:
            pass
        if self.proc:
            try:
                self.proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.proc.kill()

    def __enter__(self):
        return self.start()

    def __exit__(self, *exc):
        self.stop()

    # -- debug markers -----------------------------------------------------
    def debug_log(self):
        try:
            with open(self.debug_path, "r", errors="replace") as f:
                return f.read()
        except OSError:
            return ""

    def wait_marker(self, marker, timeout=30):
        end = time.time() + timeout
        while time.time() < end:
            log = self.debug_log()
            if marker in log:
                return True
            if "SAND_EXCEPTION:" in log or re.search(r"SAND_TEST:\w*FAIL", log):
                return False
            time.sleep(0.1)
        return False

    # -- serial console ----------------------------------------------------
    def _pump(self):
        try:
            data = self.serial.recv(65536)
            if data:
                self.serial_text += data.decode("utf-8", "replace")
        except (BlockingIOError, OSError):
            pass

    def send(self, text, delay=0.02):
        for ch in text:
            self.serial.sendall(ch.encode())
            time.sleep(delay)

    def expect(self, pattern, timeout=10, start=0):
        """Wait until regex matches serial output after offset `start`; return match."""
        end = time.time() + timeout
        rx = re.compile(pattern, re.S)
        while time.time() < end:
            self._pump()
            m = rx.search(self.serial_text, start)
            if m:
                return m
            time.sleep(0.05)
        return None

    def run_command(self, command, timeout=10):
        """Type a command at the shell prompt and return its output text."""
        mark = len(self.serial_text)
        self.send(command + "\r")
        m = self.expect(r"sand\x1b\[38;2;146;153;173m > \x1b\[0m$", timeout, start=mark + len(command))
        out = self.serial_text[mark:]
        return strip_ansi(out)

    # -- monitor -----------------------------------------------------------
    def _drain_monitor(self):
        self.mon.settimeout(0.3)
        try:
            while self.mon.recv(4096):
                pass
        except (socket.timeout, OSError):
            pass
        self.mon.settimeout(3)

    def monitor(self, command, wait=0.3):
        self.mon.sendall(command.encode() + b"\n")
        time.sleep(wait)
        self._drain_monitor()

    def sendkey(self, key):
        self.monitor(f"sendkey {key}", 0.15)

    def mouse_move(self, dx, dy):
        self.monitor(f"mouse_move {dx} {dy}", 0.1)

    def mouse_button(self, state):
        self.monitor(f"mouse_button {state}", 0.1)

    def screendump(self, path):
        self.monitor(f"screendump {path}", 1.0)
        return os.path.exists(path) and os.path.getsize(path) > 0


_ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def strip_ansi(text):
    return _ANSI.sub("", text)
