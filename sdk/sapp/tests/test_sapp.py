import importlib.util
import json
import pathlib
import struct
import tempfile
import unittest

TOOL = pathlib.Path(__file__).parents[1] / "tools" / "sapp.py"
SPEC = importlib.util.spec_from_file_location("sapp", TOOL)
sapp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sapp)


def make_elf(path):
    # ELF32 i386 with two page-aligned PT_LOAD segments: RX text and RW data+BSS.
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    header = sapp.ELF_HEADER.pack(ident, 2, 3, 1, 0x40000000, 52, 0, 0,
                                  52, 32, 2, 40, 0, 0)
    text = b"\x90\x90\xc3"
    data = b"DATA"
    ph1 = sapp.ELF_PROGRAM.pack(1, 0x1000, 0x40000000, 0, len(text), len(text), 5, 4096)
    ph2 = sapp.ELF_PROGRAM.pack(1, 0x2000, 0x40001000, 0, len(data), 4096, 6, 4096)
    blob = bytearray(0x2004)
    blob[:len(header)] = header
    blob[52:52+32] = ph1
    blob[84:84+32] = ph2
    blob[0x1000:0x1000+len(text)] = text
    blob[0x2000:0x2000+len(data)] = data
    path.write_bytes(blob)


class SappFormatTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        make_elf(self.root / "app.elf")
        (self.root / "manifest.json").write_text(json.dumps({
            "id": "org.sand.test", "name": "Test", "version": "0.2"
        }), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def pack(self, relocations=None):
        args = type("Args", (), {
            "input": str(self.root / "app.elf"),
            "manifest": str(self.root / "manifest.json"),
            "output": str(self.root / "test.sapp"),
            "relocations": relocations,
        })()
        sapp.pack(args)
        return self.root / "test.sapp"

    def test_multi_segment_and_bss_round_trip(self):
        info = sapp.parse(self.pack())
        self.assertEqual(len(info["segments"]), 2)
        self.assertEqual(info["segments"][1]["memsz"], 4096)
        self.assertEqual(info["segments"][1]["filesz"], 4)
        self.assertEqual(info["segments"][0]["permissions"], 5)
        self.assertEqual(info["segments"][1]["permissions"], 3)

    def test_relocation_table_round_trip(self):
        rel = self.root / "relocations.json"
        rel.write_text(json.dumps([{"target": 0x40001000, "addend": 0x40000000}]))
        info = sapp.parse(self.pack(str(rel)))
        self.assertEqual(info["relocations"][0]["type"], "SAND_ABS32")

    def test_rejects_bad_magic(self):
        path = self.pack()
        blob = bytearray(path.read_bytes())
        blob[:4] = b"NOPE"
        (self.root / "bad.sapp").write_bytes(blob)
        with self.assertRaisesRegex(ValueError, "magic"):
            sapp.parse(self.root / "bad.sapp")

    def test_rejects_writable_executable_elf_segment(self):
        blob = bytearray((self.root / "app.elf").read_bytes())
        # First PT_LOAD flags at program header offset + 24.
        struct.pack_into("<I", blob, 52 + 24, 7)
        (self.root / "bad.elf").write_bytes(blob)
        args = type("Args", (), {
            "input": str(self.root / "bad.elf"),
            "manifest": str(self.root / "manifest.json"),
            "output": str(self.root / "bad.sapp"),
            "relocations": None,
        })()
        with self.assertRaisesRegex(ValueError, "W\+X"):
            sapp.pack(args)


if __name__ == "__main__":
    unittest.main()
