#!/usr/bin/env python3
"""Launch a user's Windows game via the COMPLETE BoxedWine runtime.

This launcher deliberately uses BoxedWine's original Linux/Wine guest runtime,
not the emuwasm PE import-only linker. The caller supplies a Wine Linux
filesystem ZIP (e.g. a TinyCore...Wine...zip downloaded by BoxedWine).
Run this script in Linux/WSL2; it never downloads copyrighted game files.
"""
from __future__ import annotations

import argparse
import os
import shlex
import subprocess
import sys
from pathlib import Path


def build_command(
    boxedwine: Path,
    wine_root_zip: Path,
    game_directory: Path,
    exe_relative: str,
    profile_root: Path,
    resolution: str,
) -> list[str]:
    if not boxedwine.is_file():
        raise ValueError(f"BoxedWine Linux executable not found: {boxedwine}")
    if not wine_root_zip.is_file() or not wine_root_zip.name.lower().endswith(".zip"):
        raise ValueError(f"BoxedWine Linux/Wine filesystem ZIP not found: {wine_root_zip}")
    if not game_directory.is_dir():
        raise ValueError(f"Game directory not found: {game_directory}")
    if not exe_relative or Path(exe_relative).is_absolute():
        raise ValueError("--exe must be a relative path within --game-dir")

    # Never allow ../ to escape the directory mounted inside the guest.
    game_root = game_directory.resolve()
    executable = (game_root / exe_relative).resolve()
    if not executable.is_file() or not executable.is_relative_to(game_root):
        raise ValueError(f"Windows game EXE does not exist inside game directory: {exe_relative}")
    if executable.suffix.lower() != ".exe":
        raise ValueError("--exe must refer to a Windows .exe executable")

    width, separator, height = resolution.lower().partition("x")
    if not separator or not width.isdigit() or not height.isdigit():
        raise ValueError("--resolution expects WIDTHxHEIGHT, e.g. 1024x768")
    if not (320 <= int(width) <= 7680 and 200 <= int(height) <= 4320):
        raise ValueError("Guest screen resolution is outside the supported range")

    # -mount uses a real native directory and a path in BoxedWine's guest Linux FS.
    # The original BoxedWine guest Wine binary is /bin/wine.
    guest_root = "/home/username/files"
    exe_in_guest = f"{guest_root}/{executable.relative_to(game_root).as_posix()}"
    guest_working_directory = f"{guest_root}/{executable.relative_to(game_root).parent.as_posix()}"
    return [
        str(boxedwine.resolve()),
        "-root", str(profile_root.resolve()),
        "-zip", str(wine_root_zip.resolve()),
        "-mount", str(game_root), guest_root,
        "-w", guest_working_directory,
        "-resolution", f"{int(width)}x{int(height)}",
        "/bin/wine", exe_in_guest,
    ]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boxedwine", type=Path, required=True,
                        help="Original BoxedWine Linux executable (built from upstream)")
    parser.add_argument("--wine-root-zip", type=Path, required=True,
                        help="Complete BoxedWine Linux/Wine filesystem ZIP; PE DLLs alone are insufficient")
    parser.add_argument("--game-dir", type=Path, required=True,
                        help="Existing game installation folder, mounted read/write by BoxedWine")
    parser.add_argument("--exe", default="prism3d.exe",
                        help="EXE relative to --game-dir (default: prism3d.exe)")
    parser.add_argument("--profile", type=Path, default=Path("boxedwine-game-profile"),
                        help="Guest writable Linux profile directory")
    parser.add_argument("--resolution", default="1024x768")
    parser.add_argument("--dry-run", action="store_true",
                        help="Validate paths and print the command without starting the game")
    options = parser.parse_args(argv)
    try:
        command = build_command(
            options.boxedwine, options.wine_root_zip, options.game_dir,
            options.exe, options.profile, options.resolution,
        )
    except ValueError as exc:
        parser.error(str(exc))
    print("BoxedWine full Wine runtime command:", shlex.join(command), flush=True)
    if options.dry_run:
        return 0

    # Only create the profile after all input files were validated.
    options.profile.mkdir(parents=True, exist_ok=True)
    print("Launching through BoxedWine /bin/wine. A functioning display "
          "(WSLg, X11 or Wayland) and the complete Wine filesystem are required.",
          flush=True)
    try:
        return subprocess.run(command, check=False).returncode
    except OSError as exc:
        print(f"Cannot start original BoxedWine runtime: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
