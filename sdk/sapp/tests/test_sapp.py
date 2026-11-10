import importlib.util
import json
import pathlib
import tempfile
import unittest

TOOL = pathlib.Path(__file__).parents[1] / "tools" / "sapp.py"
SPEC = importlib.util.spec_from_file_location("sapp", TOOL)
sapp = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sapp)


class SappFormatTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.tmp.name)
        (self.root / "image.bin").write_bytes(b"\x90\x90\xc3")
        (self.root / "manifest.json").write_text(json.dumps({
            "id": "org.sand.test", "name": "Test", "version": "0.1"
        }), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def test_pack_and_verify_round_trip(self):
        args = type("Args", (), {
            "input": str(self.root / "image.bin"),
            "manifest": str(self.root / "manifest.json"),
            "output": str(self.root / "test.sapp"),
            "base": "0x40000000",
            "entry": "0x40000000",
            "permissions": "rx",
        })()
        sapp.pack(args)
        info = sapp.parse(args.output)
        self.assertEqual(info["architecture"], "x86-32")
        self.assertEqual(info["metadata"]["id"], "org.sand.test")
        self.assertEqual(info["segments"][0]["filesz"], 3)

    def test_rejects_wrong_magic(self):
        args = type("Args", (), {
            "input": str(self.root / "image.bin"),
            "manifest": str(self.root / "manifest.json"),
            "output": str(self.root / "test.sapp"),
            "base": "0x40000000",
            "entry": "0x40000000",
            "permissions": "rx",
        })()
        sapp.pack(args)
        blob = bytearray((self.root / "test.sapp").read_bytes())
        blob[0:4] = b"NOPE"
        (self.root / "bad.sapp").write_bytes(blob)
        with self.assertRaisesRegex(ValueError, "magic"):
            sapp.parse(self.root / "bad.sapp")

    def test_rejects_writable_executable_segment(self):
        args = type("Args", (), {
            "input": str(self.root / "image.bin"),
            "manifest": str(self.root / "manifest.json"),
            "output": str(self.root / "bad.sapp"),
            "base": "0x40000000",
            "entry": "0x40000000",
            "permissions": "rwx",
        })()
        with self.assertRaisesRegex(ValueError, "W+X"):
            sapp.pack(args)


if __name__ == "__main__":
    unittest.main()
