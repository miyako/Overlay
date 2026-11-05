#!/usr/bin/env python3
"""Explicit, bounded desktop probe; never injects input or certifies click-through."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import threading
import time
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
    teardown = parser.add_mutually_exclusive_group()
    teardown.add_argument("--cancel-after-ms", type=int, help="create an owned cancellation file")
    teardown.add_argument("--host-exit-after-ms", type=int, help="exit an owned test host, never 4D")
    parser.add_argument("--fade-in-ms", type=int, default=150)
    parser.add_argument("--fade-out-ms", type=int, default=150)
    args = parser.parse_args()
    if not 1000 <= args.timeout_ms <= 30000:
        parser.error("manual probe timeout must be 1000..30000 milliseconds")
    for delay in (args.cancel_after_ms, args.host_exit_after_ms):
        if delay is not None and not 100 <= delay < args.timeout_ms:
            parser.error("teardown delay must be at least 100 ms and less than the timeout")
    if not all(0 <= value <= 2000 for value in (args.fade_in_ms, args.fade_out_ms)):
        parser.error("manual fade durations must be 0..2000 milliseconds")
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
                   "--fade-in-ms", str(args.fade_in_ms), "--fade-out-ms", str(args.fade_out_ms)]
        command += ["--stdin"] if args.stdin else ["--image", str(path)]
        timer = None
        host = None
        if args.cancel_after_ms is not None:
            control = Path(scratch) / "cancel"
            command += ["--cancel-file", str(control)]
            timer = threading.Timer(args.cancel_after_ms / 1000, control.touch)
        if args.host_exit_after_ms is not None:
            host = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(40)"])
            command += ["--host-pid", str(host.pid)]
            timer = threading.Timer(args.host_exit_after_ms / 1000, host.terminate)
        started = time.monotonic()
        try:
            if timer is not None:
                timer.daemon = True
                timer.start()
            result = subprocess.run(command, input=image if args.stdin else None,
                                    capture_output=True, timeout=args.timeout_ms / 1000 + 10)
        except subprocess.TimeoutExpired as error:
            print(json.dumps({"check": "manual_desktop_probe", "status": "failed",
                              "reason": "helper exceeded bounded lifetime",
                              "stdout": (error.stdout or b"").decode("utf-8", errors="replace"),
                              "stderr": (error.stderr or b"").decode("utf-8", errors="replace")}))
            return 1
        finally:
            if timer is not None:
                timer.cancel()
                timer.join()
            if host is not None:
                if host.poll() is None:
                    host.terminate()
                try:
                    host.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    host.kill()
                    host.wait(timeout=5)
        elapsed_ms = round((time.monotonic() - started) * 1000)
    print(json.dumps({"check": "manual_desktop_probe",
                      "exitCode": result.returncode,
                      "stdout": result.stdout.decode("utf-8", errors="strict"),
                      "stderr": result.stderr.decode("utf-8", errors="strict"),
                      "elapsed_ms": elapsed_ms,
                      "gui_behavior": "requires human observation"}))
    return 0 if result.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
