#!/usr/bin/env python3
"""Create a browser-ready ZIP from YOUR OWN Windows game's installation.

BoxedWine's web shell mounts ?app=prism3d as prism3d.zip. The archive
must contain prism3d.exe and its data directly at its root (no extra folder).
This script does not provide Wine's Linux root filesystem: boxedwine.zip
must come from a separate compatible BoxedWine installation.
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile


def package_game(game_dir: Path, archive_path: Path, exe: str) -> tuple[int, int]:
    source = game_dir.resolve()
    destination = archive_path.resolve()
    if not source.is_dir():
        raise ValueError(f"Game folder not found: {source}")
    if not exe or Path(exe).is_absolute():
        raise ValueError("EXE must be relative to the game folder")
    entry = (source / exe).resolve()
    if not entry.is_relative_to(source) or not entry.is_file() or entry.suffix.lower() != ".exe":
        raise ValueError(f"EXE not found inside the game folder: {exe}")
    if destination == entry:
        raise ValueError("Output ZIP cannot overwrite the executable")

    collected: list[tuple[Path, Path]] = []
    total_bytes = 0
    for base, directories, files in os.walk(source, followlinks=False):
        current = Path(base)
        # Do not silently include host files through symlinks.
        for directory in list(directories):
            if (current / directory).is_symlink():
                raise ValueError(f"Symbolic-link directories are not supported: {current / directory}")
        for filename in files:
            path = current / filename
            if path.is_symlink():
                raise ValueError(f"Symbolic-link files are not supported: {path}")
            if path.resolve() == destination:
                continue
            if not path.is_file():
                continue
            relative = path.relative_to(source)
            if relative.parts[0] in {".git", "__pycache__"}:
                continue
            collected.append((path, relative))
            total_bytes += path.stat().st_size
    if not any(p.resolve() == entry for p, _ in collected):
        raise ValueError("Game executable was skipped while collecting files")
    if len(collected) > 200000:
        raise ValueError("More than 200,000 files: refusing to create an oversized browser archive")
    if total_bytes >= 4 * 1024**3:
        raise ValueError("Input exceeds 4 GiB: browser ZIP packaging needs splitting/streaming")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Sorted order makes the ZIP contents easier to inspect and compare.
    with ZipFile(destination, "w", compression=ZIP_DEFLATED, compresslevel=6, allowZip64=True) as target:
        for path, relative in sorted(collected, key=lambda item: item[1].as_posix().casefold()):
            target.write(path, relative.as_posix())
    with ZipFile(destination) as verify:
        if verify.testzip() is not None:
            raise ValueError("ZIP verification failed")
        if Path(exe).as_posix() not in verify.namelist():
            raise ValueError("Packaged ZIP has no EXE at its requested path")
    return len(collected), total_bytes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", type=Path, required=True,
                        help="Directory of YOUR installed Windows game")
    parser.add_argument("--exe", default="prism3d.exe",
                        help="EXE path relative to the game directory")
    parser.add_argument("--output", type=Path, default=Path("prism3d.zip"))
    options = parser.parse_args(argv)
    try:
        count, size = package_game(options.game_dir, options.output, options.exe)
    except (OSError, ValueError) as exc:
        print(f"Packaging failed: {exc}", file=sys.stderr)
        return 2
    print(f"Created {options.output.resolve()}: {count} files, "
          f"{size:,} uncompressed bytes")
    print("Copy this ZIP next to boxedwine.html, boxedwine.wasm, boxedwine.js "
          "and a separately obtained compatible boxedwine.zip root filesystem.")
    print(f"Open: boxedwine.html?app={options.output.stem}&p={options.exe}")
    print("Browser compatibility and Prism3D graphics support are not guaranteed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
