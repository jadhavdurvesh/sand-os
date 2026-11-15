#!/usr/bin/env python3
"""Install a SAPP app into a running SAND OS (or bake it into a disk image) over the serial console.

  Live system (QEMU started with:  -serial tcp:127.0.0.1:4555,server=on,wait=off  -- see `make run-serial`):
      tools/sand_install.py --connect 127.0.0.1:4555 [--run] <name> <app.sapp>

  Offline image (boots a headless copy, installs, shuts down cleanly, writes the result):
      tools/sand_install.py --boot build/sand-os.img --out my-image.img [--run] <name> <app.sapp>

The kernel decides: it refuses invalid names, names of built-in apps, non-SAPP data and anything the launch
gate denies; this tool only moves bytes (hex lines through the shell's `install` command) and reports.
Exit status: 0 installed, 1 refused or failed, 2 usage / connection problems.
"""
import argparse
import os
import re
import shutil
import socket
import sys
import time

ANSI = re.compile(r"\x1b\[[0-9;]*m")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "qemu"))


class SerialConsole:
    """Minimal console over a TCP serial port (same send/expect surface as tests/qemu/sand_qemu.SandVM)."""

    def __init__(self, host, port):
        self.serial = socket.create_connection((host, port), timeout=5)
        self.serial.setblocking(False)
        self.serial_text = ""

    def _pump(self):
        try:
            data = self.serial.recv(65536)
            if data:
                self.serial_text += data.decode("utf-8", "replace")
        except (BlockingIOError, OSError):
            pass

    def send(self, text, delay=0.004):
        for ch in text:
            self.serial.sendall(ch.encode())
            time.sleep(delay)

    def expect(self, pattern, timeout=10, start=0):
        end, rx = time.time() + timeout, re.compile(pattern, re.S)
        while time.time() < end:
            self._pump()
            m = rx.search(self.serial_text, start)
            if m:
                return m
            time.sleep(0.05)
        return None


def clean(text):
    return ANSI.sub("", text).replace("\r", "")


def install(con, name, data, log=print):
    """Returns (ok, message). Works on any object with send/expect/serial_text."""
    mark = len(con.serial_text)
    con.send(f"install {name}\r")
    if not con.expect(r"send hex lines", 10, start=mark):
        return False, "the shell did not offer the installer: " + clean(con.serial_text[mark:])[-200:].strip()
    for off in range(0, len(data), 100):
        mark = len(con.serial_text)
        con.send(data[off:off + 100].hex() + "\r")
        got = con.expect(r"\.|install: [^\n]*", 10, start=mark + 1)   # skip the echoed hex (it never contains '.')
        if not got or got.group(0) != ".":
            return False, clean(con.serial_text[mark:])[-300:].strip()
        if (off // 100) % 20 == 0:
            log(f"  sent {min(off + 100, len(data))}/{len(data)} bytes")
    mark = len(con.serial_text)
    con.send("end\r")
    m = con.expect(r"(installed [^\n]*|install: [^\n]*)", 15, start=mark + 3)
    if not m:
        return False, "no answer after 'end'"
    text = clean(m.group(0)).strip()
    return text.startswith("installed"), text


def run_app(con, name, timeout=30):
    mark = len(con.serial_text)
    con.send(f"run {name}\r")
    con.expect(r"exited with status|killed by CPU fault|cannot start", timeout, start=mark + 4)
    return clean(con.serial_text[mark:]).strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    where = ap.add_mutually_exclusive_group(required=True)
    where.add_argument("--connect", metavar="HOST:PORT", help="serial TCP port of a running SAND OS")
    where.add_argument("--boot", metavar="IMAGE", help="boot a headless copy of this disk image")
    ap.add_argument("--out", metavar="IMAGE", help="with --boot: write the modified image here (default: discard)")
    ap.add_argument("--run", action="store_true", help="run the app after installing and print its output")
    ap.add_argument("name", help="app name: [a-z][a-z0-9]{0,15}")
    ap.add_argument("sapp", help="path to the .sapp image (see sdk/sapp/tools)")
    args = ap.parse_args()
    try:
        data = open(args.sapp, "rb").read()
    except OSError as e:
        ap.exit(2, f"sand_install: {e}\n")
    if args.out and not args.boot:
        ap.exit(2, "sand_install: --out only makes sense with --boot\n")

    vm = None
    try:
        if args.connect:
            host, _, port = args.connect.rpartition(":")
            con = SerialConsole(host or "127.0.0.1", int(port))
            con.send("\r")                                   # provoke a prompt
        else:
            from sand_qemu import SandVM
            work = args.out or args.boot + ".scratch"
            if os.path.abspath(work) == os.path.abspath(args.boot):
                ap.exit(2, "sand_install: --out must differ from the source image\n")
            shutil.copyfile(args.boot, work)
            vm = con = SandVM(work).start()
            if not vm.wait_marker("SAND_TEST:M9_SHELL_STARTED", 60):
                print("sand_install: the system did not reach its shell", file=sys.stderr); return 2
        if not con.expect(r"sand\S* > ", 30):
            print("sand_install: no shell prompt (is another program using the console?)", file=sys.stderr); return 2
        ok, message = install(con, args.name, data)
        print(("OK: " if ok else "REFUSED: ") + message)
        if ok and args.run:
            print(run_app(con, args.name))
        return 0 if ok else 1
    except (OSError, ValueError) as e:
        print(f"sand_install: {e}", file=sys.stderr); return 2
    finally:
        if vm:
            vm.stop()
            if not args.out:
                try: os.unlink(args.boot + ".scratch")
                except OSError: pass


if __name__ == "__main__":
    sys.exit(main())
