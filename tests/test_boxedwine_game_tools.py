"""Offline regression tests for native/WSL game launches and browser ZIP layout."""
from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from package_boxedwine_game import package_game  # noqa: E402
from run_boxedwine_game import build_command  # noqa: E402


class GameToolsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / "game"
        self.game.mkdir()
        (self.game / "prism3d.exe").write_bytes(b"MZsynthetic-test-only")
        (self.game / "p3shared.dll").write_bytes(b"dummy-dll")
        (self.game / "assets").mkdir()
        (self.game / "assets" / "game.dat").write_bytes(b"resource data")
        self.boxedwine = self.root / "boxedwine"
        self.boxedwine.write_bytes(b"binary placeholder")
        self.rootfs = self.root / "TinyCore15Wine11.0.zip"
        self.rootfs.write_bytes(b"rootfs placeholder")

    def test_zip_root_and_assets(self):
        output = self.root / "prism3d.zip"
        count, total = package_game(self.game, output, "prism3d.exe")
        self.assertEqual(count, 3)
        self.assertGreater(total, 0)
        with ZipFile(output) as archive:
            self.assertEqual(set(archive.namelist()), {
                "prism3d.exe", "p3shared.dll", "assets/game.dat"
            })
            self.assertEqual(archive.read("assets/game.dat"), b"resource data")

    def test_no_output_recursion(self):
        output = self.game / "prism3d.zip"
        package_game(self.game, output, "prism3d.exe")
        package_game(self.game, output, "prism3d.exe")
        with ZipFile(output) as archive:
            self.assertNotIn("prism3d.zip", archive.namelist())

    def test_entry_cannot_escape(self):
        with self.assertRaises(ValueError):
            package_game(self.game, self.root / "out.zip", "../other.exe")
        with self.assertRaises(ValueError):
            package_game(self.game, self.root / "out.zip", "no.exe")

    def test_nested_guest_windows_entry(self):
        (self.game / "bin").mkdir()
        (self.game / "bin" / "game.exe").write_bytes(b"MZ")
        command = build_command(
            self.boxedwine, self.rootfs, self.game, "bin/game.exe",
            self.root / "profile", "1280x720",
        )
        self.assertEqual(command[1:3], ["-root", str((self.root / "profile").resolve())])
        self.assertEqual(command[3:5], ["-zip", str(self.rootfs.resolve())])
        self.assertEqual(command[5:8], [
            "-mount", str(self.game.resolve()), "/home/username/files",
        ])
        self.assertEqual(command[8:10], ["-w", "/home/username/files/bin"])
        self.assertEqual(command[-2:], [
            "/bin/wine", "/home/username/files/bin/game.exe",
        ])
        self.assertEqual(command[10:12], ["-resolution", "1280x720"])

    def test_disallow_missing_wine_root(self):
        with self.assertRaisesRegex(ValueError, "filesystem ZIP"):
            build_command(self.boxedwine, self.root / "missing.zip",
                          self.game, "prism3d.exe", self.root / "profile", "1024x768")

    def test_disallow_invalid_entry_or_screen(self):
        with self.assertRaises(ValueError):
            build_command(self.boxedwine, self.rootfs, self.game, "../outside.exe",
                          self.root / "profile", "1024x768")
        with self.assertRaises(ValueError):
            build_command(self.boxedwine, self.rootfs, self.game, "prism3d.exe",
                          self.root / "profile", "bad")


if __name__ == "__main__":
    unittest.main()
