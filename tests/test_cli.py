"""Native, noninteractive Overlay protocol regressions (no desktop input injection)."""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib


ROOT = Path(__file__).resolve().parents[1]


def png(width, height, rgba):
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(
            ">I", zlib.crc32(kind + value))
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4]
                    for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class CppCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.environ.get("TEST_CLI_LANGUAGE") == "none":
            raise unittest.SkipTest("native toolchains disabled explicitly")
        if os.environ.get("TEST_CLI_LANGUAGE", "cpp") != "cpp":
            raise RuntimeError("Overlay requires the selected C++ toolchain")
        scratch = tempfile.TemporaryDirectory(prefix=".overlay-cli path ", dir=ROOT / "tests")
        cls.addClassCleanup(scratch.cleanup)
        cls.root = Path(scratch.name)
        build = cls.root / "build"
        subprocess.run(["cmake", "-S", str(ROOT / "cli"), "-B", str(build),
                        "-DCMAKE_BUILD_TYPE=Release",
                        *(["-A", "x64"] if os.name == "nt" else [])],
                       check=True, capture_output=True, timeout=180)
        subprocess.run(["cmake", "--build", str(build), "--config", "Release"],
                       check=True, capture_output=True, timeout=180)
        name = "overlay-cli" + (".exe" if os.name == "nt" else "")
        built = build / name
        if not built.exists():
            built = build / "Release" / name
        cls.executable = cls.root / name
        shutil.copy2(built, cls.executable)
        cls.image = cls.root / 'image with spaces.png'
        cls.image.write_bytes(png(2, 2, b"\xff\0\0\xff" * 4))

    def run_cli(self, args, data=None, timeout=10):
        return subprocess.run([os.fspath(self.executable), *args], input=data,
                              capture_output=True, timeout=timeout)

    def assert_error(self, args, code=None, data=None):
        result = self.run_cli(args, data)
        self.assertNotEqual(result.returncode, 0, result)
        self.assertEqual(result.stdout, b"")
        self.assertTrue(result.stderr.endswith(b"\n"), result)
        value = json.loads(result.stderr.decode("utf-8"))
        self.assertEqual(value["protocol"], 1)
        self.assertIsInstance(value["error"]["code"], str)
        self.assertIsInstance(value["error"]["message"], str)
        if code is not None:
            self.assertEqual(value["error"]["code"], code)
        return value

    def test_version(self):
        result = self.run_cli(["--version"])
        self.assertEqual((result.returncode, result.stdout, result.stderr),
                         (0, b'{"protocol":1,"version":"0.1.0"}\n', b""))

    def test_capabilities(self):
        result = self.run_cli(["capabilities"])
        self.assertEqual(result.returncode, 0, result)
        self.assertEqual(result.stderr, b"")
        value = json.loads(result.stdout)
        self.assertEqual(value, {"protocol": 1, "modal": False, "format": "png",
                                 "modes": ["passive", "clickable"]})

    def test_validate_real_png_path_and_binary_stdin_without_window(self):
        displays = self.run_cli(["screens"])
        self.assertEqual(displays.returncode, 0, displays)
        screens = json.loads(displays.stdout)["screens"]
        if not screens:
            self.skipTest("no connected native screen for placement validation")
        screen = screens[0]
        position = ["--x", str(int(screen["x"])), "--y", str(int(screen["y"]))]
        expected = b'{"protocol":1,"valid":true,"width":2,"height":2}\n'
        for source, data in ((["--image", str(self.image)], None),
                             (["--stdin"], self.image.read_bytes())):
            result = self.run_cli(["validate", *source, *position], data=data)
            self.assertEqual((result.returncode, result.stdout, result.stderr),
                             (0, expected, b""))

    def test_contract_vectors(self):
        import sys
        sys.path.insert(0, str(ROOT / "scripts"))
        from build_cli import smoke_test
        smoke_test(self.executable, json.loads((ROOT / "template.json").read_text())["version"],
                   ROOT / "tests/cli-contract.json")

    def test_invalid_options(self):
        for args in ([], [""], ["unknown"], ["--version", "extra"],
                     ["show"], ["show", "--image"], ["show", "--unknown", "x"],
                     ["show", "--x", "0", "--x", "1"],
                     ["show", "--image", str(self.image), "--stdin"],
                     ["show", "--mode", "modal"], ["show", "--modal", "maybe"]):
            with self.subTest(args=args):
                self.assert_error(args)

    def test_modal_is_explicitly_unsupported(self):
        self.assert_error(["show", "--modal", "true"], "unsupported_modal")

    def test_numeric_limits(self):
        for option, values in {
            "--x": ("nan", "inf", "1e309", "2147483648", "-2147483649"),
            "--y": ("NaN", "-inf"),
            "--scale": ("0", "-1", "nan", "1000000000"),
            "--opacity": ("-1", "1.1", "nan"),
            "--timeout-ms": ("-1", "nan", "2147483648"),
            "--fade-in-ms": ("-1", "999999999"),
            "--fade-out-ms": ("-1", "999999999"),
            "--screen": ("-1", "1.5", "nan"),
            "--host-pid": ("0", "-1", "nan"),
        }.items():
            for value in values:
                with self.subTest(option=option, value=value):
                    args = ["validate", "--image", str(self.image)]
                    if option != "--x":
                        args += ["--x", "0"]
                    if option != "--y":
                        args += ["--y", "0"]
                    self.assert_error([*args, option, value], "invalid_arguments")

    def test_empty_missing_malformed_images(self):
        empty = self.root / "empty.png"
        empty.write_bytes(b"")
        malformed = self.root / "not png.png"
        malformed.write_bytes(b"not png")
        for path in (empty, malformed, self.root / "missing.png"):
            with self.subTest(path=path):
                self.assert_error(["validate", "--image", str(path), "--x", "0", "--y", "0"])
        self.assert_error(["validate", "--stdin", "--x", "0", "--y", "0"], data=b"")
        self.assert_error(["validate", "--stdin", "--x", "0", "--y", "0"], data=b"invalid png")

    def test_transparent_image(self):
        image = self.root / "transparent.png"
        image.write_bytes(png(2, 2, b"\xff\0\0\0" * 4))
        self.assert_error(["validate", "--image", str(image), "--x", "0", "--y", "0"],
                          "invalid_image")

    def test_excessive_dimensions_before_decode(self):
        image = self.root / "huge.png"
        for width, height in ((8193, 1), (8192, 2049), (0, 1), (1, 0)):
            data = bytearray(png(1, 1, b"\xff\0\0\xff"))
            data[16:24] = struct.pack(">II", width, height)
            data[29:33] = struct.pack(">I", zlib.crc32(data[12:29]))
            image.write_bytes(data)
            with self.subTest(width=width, height=height):
                self.assert_error(["validate", "--image", str(image), "--x", "0", "--y", "0"],
                                  "image_limit")

    @unittest.skipUnless(os.name == "posix", "byte argv are POSIX-specific")
    def test_malformed_utf8(self):
        for value in (b"\xff", b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80"):
            with self.subTest(value=value):
                self.assert_error([b"validate", b"--image", value, b"--x", b"0", b"--y", b"0"],
                                  "invalid_arguments")


if __name__ == "__main__":
    unittest.main()
