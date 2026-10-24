#!/usr/bin/env python3
"""Regression tests for the Wine i386 PE32 DLL packager."""
import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("collect_wine_dlls.py")
spec = importlib.util.spec_from_file_location("collect_wine_dlls", MODULE_PATH)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def write_fixture(path: Path, machine: int, optional_magic: int, signature: bytes) -> None:
    # Minimal PE header for testing the validator (not an executable DLL).
    data = bytearray(512)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = signature
    struct.pack_into("<H", data, 0x84, machine)
    struct.pack_into("<H", data, 0x98, optional_magic)
    path.write_bytes(data)


class WinePe32ValidationTests(unittest.TestCase):
    def test_real_pe32_signature_and_machine(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ntdll.dll"
            write_fixture(path, 0x014C, 0x010B, bytes((0x50, 0x45, 0, 0)))
            self.assertTrue(module.is_pe32_x86(path))

    def test_reject_pe32_plus_64bit(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ntdll.dll"
            write_fixture(path, 0x8664, 0x020B, bytes((0x50, 0x45, 0, 0)))
            self.assertFalse(module.is_pe32_x86(path))

    def test_reject_escaped_signature(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ntdll.dll"
            write_fixture(path, 0x014C, 0x010B, b"PE\\")
            self.assertFalse(module.is_pe32_x86(path))

    def test_reject_elf(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "ntdll.dll"
            path.write_bytes(bytes((0x7F, 0x45, 0x4C, 0x46)) + bytes(508))
            self.assertFalse(module.is_pe32_x86(path))


if __name__ == "__main__":
    unittest.main()
