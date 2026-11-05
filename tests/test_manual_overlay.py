import importlib.util
from pathlib import Path
import struct
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("manual_overlay", ROOT / "scripts/manual_overlay.py")
manual = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manual)


class ManualFixtureTests(unittest.TestCase):
    def test_png_is_rgba_with_exact_alpha_zero_hole(self):
        data = manual.fixture_png()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        chunks = {}
        while offset < len(data):
            length = struct.unpack_from(">I", data, offset)[0]
            kind = data[offset + 4:offset + 8]
            value = data[offset + 8:offset + 8 + length]
            crc = struct.unpack_from(">I", data, offset + 8 + length)[0]
            self.assertEqual(crc, zlib.crc32(kind + value))
            chunks[kind] = value
            offset += length + 12
        self.assertEqual(offset, len(data))
        self.assertEqual(struct.unpack(">IIBBBBB", chunks[b"IHDR"]),
                         (180, 120, 8, 6, 0, 0, 0))
        rows = zlib.decompress(chunks[b"IDAT"])
        self.assertEqual(len(rows), 120 * (1 + 180 * 4))
        for y in range(120):
            start = y * (1 + 180 * 4)
            self.assertEqual(rows[start], 0)
            for x in range(180):
                alpha = rows[start + 1 + x * 4 + 3]
                self.assertEqual(alpha, 0 if 50 <= x < 130 and 30 <= y < 90 else 255)
