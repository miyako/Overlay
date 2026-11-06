import importlib.util
import contextlib
import io
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch
import zlib


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("manual_overlay", ROOT / "scripts/manual_overlay.py")
manual = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manual)


class ManualFixtureTests(unittest.TestCase):
    def run_probe(self, options):
        with tempfile.TemporaryDirectory() as scratch:
            executable = Path(scratch) / "helper"
            executable.touch()
            completed = subprocess.CompletedProcess([], 0, b'{"protocol":1}\n', b"")
            host = Mock(pid=12345)
            host.poll.return_value = None
            timer = Mock()
            arguments = ["manual_overlay.py", "--executable", str(executable),
                         "--x", "100", "--y", "100", *options]
            with patch.object(manual.sys, "argv", arguments), \
                    patch.object(manual.subprocess, "run", return_value=completed) as run, \
                    patch.object(manual.subprocess, "Popen", return_value=host) as popen, \
                    patch.object(manual.threading, "Timer", return_value=timer) as schedule, \
                    contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(manual.main(), 0)
                return run.call_args, popen.call_args, schedule.call_args, timer, host

    def test_control_file_cancellation_is_owned_and_bounded(self):
        run, popen, schedule, timer, _ = self.run_probe(["--cancel-after-ms", "500"])
        command = run.args[0]
        self.assertIn("--cancel-file", command)
        self.assertIn("--timeout-ms", command)
        self.assertEqual(run.kwargs["timeout"], 20)
        self.assertEqual(schedule.args[0], 0.5)
        self.assertIsNone(popen)
        timer.start.assert_called_once()
        timer.cancel.assert_called_once()
        timer.join.assert_called_once()

    def test_host_exit_only_terminates_created_host(self):
        run, popen, schedule, timer, host = self.run_probe(["--host-exit-after-ms", "500"])
        command = run.args[0]
        self.assertEqual(command[command.index("--host-pid") + 1], "12345")
        self.assertEqual(popen.args[0][0], manual.sys.executable)
        self.assertEqual(schedule.args[1], host.terminate)
        host.terminate.assert_called_once()
        host.wait.assert_called_once_with(timeout=5)
        host.kill.assert_not_called()
        timer.cancel.assert_called_once()

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
