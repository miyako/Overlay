#!/usr/bin/env python3
"""Explicit, bounded desktop probe; never injects input or certifies click-through."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import zlib


def fixture_png():
    width, height = 180, 120
    pixels = bytearray()
    for y in range(height):
        pixels.append(0)
        for x in range(width):
            alpha = 0 if 50 <= x < 130 and 30 <= y < 90 else 255
            pixels.extend((40, 130, 220, alpha))

    def chunk(kind, value):
        return (struct.pack(">I", len(value)) + kind + value
                + struct.pack(">I", zlib.crc32(kind + value)))

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--x", type=int, required=True, help="native desktop X")
    parser.add_argument("--y", type=int, required=True, help="native desktop Y")
    parser.add_argument("--mode", choices=("passive", "clickable"), default="clickable")
    parser.add_argument("--stdin", action="store_true", help="exercise binary PNG input")
    parser.add_argument("--timeout-ms", type=int, default=10000)
    args = parser.parse_args()
    if not 1000 <= args.timeout_ms <= 30000:
        parser.error("manual probe timeout must be 1000..30000 milliseconds")
    executable = args.executable.resolve(strict=True)
    print("Place a clickable control in ANOTHER application under the requested rectangle.")
    print("180 x 120 image; transparent hole x=50..129, y=30..89.")
    print("Click the hole: the underlying application must receive the click and the overlay")
    print("must remain. Click the blue border: clickable mode must accept with local x/y.")
    print("Passive mode: the entire image must pass input without focus theft.")
    print("Observe focus, Escape when focused, and cross-app behavior yourself.")
    print("This command reports process output only; it cannot certify those observations.",
          flush=True)
    with tempfile.TemporaryDirectory(prefix="Overlay manual PNG ") as scratch:
        path = Path(scratch) / "alpha-hole.png"
        image = fixture_png()
        path.write_bytes(image)
        command = [str(executable), "show", "--x", str(args.x), "--y", str(args.y),
                   "--mode", args.mode, "--timeout-ms", str(args.timeout_ms),
                   "--fade-in-ms", "150", "--fade-out-ms", "150"]
        command += ["--stdin"] if args.stdin else ["--image", str(path)]
        try:
            result = subprocess.run(command, input=image if args.stdin else None,
                                    capture_output=True, timeout=args.timeout_ms / 1000 + 10)
        except subprocess.TimeoutExpired as error:
            print(json.dumps({"check": "manual_desktop_probe", "status": "failed",
                              "reason": "helper exceeded bounded lifetime",
                              "stdout": (error.stdout or b"").decode("utf-8", errors="replace"),
                              "stderr": (error.stderr or b"").decode("utf-8", errors="replace")}))
            return 1
    print(json.dumps({"check": "manual_desktop_probe",
                      "exitCode": result.returncode,
                      "stdout": result.stdout.decode("utf-8", errors="strict"),
                      "stderr": result.stderr.decode("utf-8", errors="strict"),
                      "gui_behavior": "requires human observation"}))
    return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
