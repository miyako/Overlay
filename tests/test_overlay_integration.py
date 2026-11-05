"""Destination-specific build/version integration, independent of desktop access."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
spec = importlib.util.spec_from_file_location("overlay_build_cli", ROOT / "scripts/build_cli.py")
build_cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_cli)


class OverlayIntegrationTests(unittest.TestCase):
    def test_exact_identity(self):
        config = json.loads((ROOT / "template.json").read_text())
        self.assertTrue(config["initialized"])
        self.assertEqual(config["repository"], "Overlay")
        self.assertEqual(config["namespace"], "Overlay")
        self.assertEqual(config["feature_class"], "Overlay")
        self.assertEqual(config["cli"], "overlay-cli")
        self.assertEqual(config["language"], "cpp")

    def test_feature_version_registry_remains_synchronized(self):
        import bump_release
        current = json.loads((ROOT / "template.json").read_text())["version"]
        major, minor, patch = map(int, current.split("."))
        version = f"{major}.{minor}.{patch + 1}"
        config, updates = bump_release.plan(ROOT, version)
        self.assertEqual(config["version"], current)
        self.assertEqual(set(updates), {
            "template.json", "package.json", "README.md", "cli/main.cpp", "cli/README.md",
            "component/Overlay/Documentation/Classes/Overlay.md",
            "tests/host/Project/Sources/Methods/Smoke.4dm",
            "tests/host/contract.json", "tests/test_cli.py",
        })

    def test_real_compiler_metadata_recorded(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            metadata = root / "CMakeFiles/4.4.3/CMakeCXXCompiler.cmake"
            metadata.parent.mkdir(parents=True)
            metadata.write_text('set(CMAKE_CXX_COMPILER_ID "AppleClang")\n'
                                'set(CMAKE_CXX_COMPILER_VERSION "21.0.0.21000001")\n')
            self.assertEqual(build_cli.cmake_toolchain(root), "AppleClang 21.0.0.21000001")
            metadata.write_text('set(CMAKE_CXX_COMPILER_ID "MSVC")\n'
                                'set(CMAKE_CXX_COMPILER_VERSION "19.40")\n')
            self.assertEqual(build_cli.cmake_toolchain(root), "MSVC 19.40")
            metadata.write_text('set(CMAKE_CXX_COMPILER_ID "MSVC")\n')
            with self.assertRaisesRegex(ValueError, "VERSION"):
                build_cli.cmake_toolchain(root)

    def test_missing_compiler_metadata_fails(self):
        with tempfile.TemporaryDirectory() as scratch:
            with self.assertRaisesRegex(ValueError, "exactly one"):
                build_cli.cmake_toolchain(Path(scratch))


if __name__ == "__main__":
    unittest.main()
