"""Synthetic ELF i386 tests; never use Windows PE DLLs as /bin/wine."""
from __future__ import annotations
import importlib.util
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

MODULE = Path(__file__).resolve().parents[1] / "scripts" / "prepare_boxedwine_wine_root.py"
spec = importlib.util.spec_from_file_location("prepare_wine", MODULE)
assert spec and spec.loader
wine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wine)


def fake_elf(machine=3):
    return b"\x7fELF" + bytes((1, 1, 1, 0)) + bytes(10) + machine.to_bytes(2, "little") + bytes(60)


class WineFilesystemBuilderTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.src = Path(self.tmp.name) / "source.zip"
        self.dst = Path(self.tmp.name) / "boxedwine.zip"

    def build(self, files):
        with ZipFile(self.src, "w") as z:
            for name, data in files.items():
                z.writestr(name, data)
        return wine.prepare(self.src, self.dst)

    def test_creates_genuine_bin_wine_from_opt(self):
        self.assertEqual(self.build({
            "opt/wine/bin/wine": fake_elf(),
            "opt/wine/lib/wine/i386-unix/ntdll.so": b"library",
            "lib/": b"",
        }), "opt/wine/bin/wine")
        with ZipFile(self.dst) as z:
            self.assertEqual(z.read("bin/wine"), fake_elf())
            self.assertEqual(z.read("opt/wine/bin/wine"), fake_elf())
            self.assertIn("opt/wine/lib/wine/i386-unix/ntdll.so", z.namelist())

    def test_does_not_replace_existing_bin_wine(self):
        self.build({"bin/wine": fake_elf(), "lib/libc.so": b"library"})
        with ZipFile(self.dst) as z:
            self.assertEqual(z.namelist().count("bin/wine"), 1)

    def test_rejects_windows_dll(self):
        with self.assertRaisesRegex(ValueError, "ELF"):
            self.build({"opt/wine/bin/wine": b"MZ" + bytes(100), "lib/libc.so": b"lib"})
        self.assertFalse(self.dst.exists())

    def test_rejects_wrong_cpu_arch(self):
        with self.assertRaisesRegex(ValueError, "ELF"):
            self.build({"opt/wine/bin/wine": fake_elf(62), "lib/libc.so": b"lib"})

    def test_rejects_wine_exe_without_runtime(self):
        with self.assertRaisesRegex(ValueError, "bibliotecas"):
            self.build({"opt/wine/bin/wine": fake_elf()})

    def test_rejects_missing_linux_executable(self):
        with self.assertRaisesRegex(ValueError, "Nao foi encontrado"):
            self.build({"wine/i386-windows/kernel32.dll": b"MZ"})

    def test_rejects_same_source_and_destination(self):
        with self.assertRaisesRegex(ValueError, "diferentes"):
            wine.prepare(self.src, self.src)


if __name__ == "__main__":
    unittest.main()
