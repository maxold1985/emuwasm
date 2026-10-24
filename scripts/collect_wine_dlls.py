#!/usr/bin/env python3
"""Copy verified Wine x86 PE DLLs into an EmuWasm distribution.

Input can be a Wine source build tree or an installed distro's
i386-windows directory. Never package Unix ELF .so libraries as PE DLLs.
"""
import argparse
import hashlib
import json
import shutil
import struct
from pathlib import Path

MAX_PE_BYTES = 256 * 1024 * 1024


def is_pe32_x86(path: Path) -> bool:
    if not path.is_file() or path.is_symlink():
        return False
    size = path.stat().st_size
    if size < 256 or size > MAX_PE_BYTES:
        return False
    with path.open("rb") as source:
        dos = source.read(64)
        if dos[:2] != b"MZ":
            return False
        pe_offset = struct.unpack_from("<I", dos, 0x3C)[0]
        if pe_offset < 64 or pe_offset + 26 > size:
            return False
        source.seek(pe_offset)
        header = source.read(26)
        # PE signature, IMAGE_FILE_MACHINE_I386 and PE32 optional header.
        return (
            len(header) == 26
            and header[:4] == b"PE\\0\\0"
            and struct.unpack_from("<H", header, 4)[0] == 0x014C
            and struct.unpack_from("<H", header, 24)[0] == 0x010B
        )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--origin", required=True)
    parser.add_argument("--min-dlls", type=int, default=3)
    args = parser.parse_args()

    # Copy DLLs only. Source tree may contain build artifacts for other
    # architectures; the PE header check prevents accidental mixing.
    files = sorted(args.source.rglob("*.dll"))
    dlls = [p for p in files if is_pe32_x86(p)]
    if len(dlls) < args.min_dlls:
        raise SystemExit(
            f"Expected >= {args.min_dlls} genuine i386 PE32 DLLs, "
            f"found {len(dlls)} in {args.source}"
        )

    target = args.output / "wine" / "i386-windows"
    target.mkdir(parents=True, exist_ok=True)
    manifest = []
    names = set()
    for path in dlls:
        key = path.name.lower()
        if key in names:
            continue
        names.add(key)
        destination = target / key
        shutil.copyfile(path, destination)
        manifest.append(
            {
                "name": key,
                "bytes": destination.stat().st_size,
                "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(),
            }
        )

    if len(manifest) < args.min_dlls:
        raise SystemExit("Not enough unique Wine PE DLLs")
    metadata = {
        "origin": args.origin,
        "architecture": "i386",
        "format": "PE32",
        "dll_count": len(manifest),
        "dlls": manifest,
    }
    (args.output / "wine" / "manifest.json").write_text(
        json.dumps(metadata, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Bundled {len(manifest)} Wine i386 PE32 DLLs from {args.origin}")


if __name__ == "__main__":
    main()
