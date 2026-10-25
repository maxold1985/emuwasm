#!/usr/bin/env python3
"""Validate a complete BoxedWine Wine filesystem and optionally add /bin/wine.

Never creates a fake Wine binary: the source must contain a real ELF i386 Wine
loader plus its runtime libraries. Supports upstream Wine 11 /opt/wine/bin/wine.

Usage:
    python scripts/prepare_boxedwine_wine_root.py --input Wine-11.0.zip --output boxedwine.zip

The resulting archive is self-contained only if the source is self-contained.
"""
from __future__ import annotations
import argparse
import os
import stat
import struct
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED

CANDIDATES = ("bin/wine", "opt/wine/bin/wine", "usr/bin/wine")
CHUNK = 1024 * 1024


def elf32_i386(data: bytes) -> bool:
    return (len(data) >= 20 and data[:4] == b"\x7fELF" and
            data[4] == 1 and data[5] == 1 and
            struct.unpack_from("<H", data, 18)[0] == 3)


def prepare(source: Path, destination: Path) -> str:
    if source.resolve() == destination.resolve():
        raise ValueError("O arquivo de entrada e o de saida precisam ser diferentes")
    with ZipFile(source) as archive:
        names = {entry.filename for entry in archive.infolist()}
        matches = [name for name in CANDIDATES if name in names]
        if not matches:
            raise ValueError("Nao foi encontrado bin/wine nem opt/wine/bin/wine. "
                             "O ZIP precisa conter o Wine Linux i386 compilado.")
        chosen = matches[0]
        with archive.open(chosen) as stream:
            header = stream.read(64)
        if not elf32_i386(header):
            raise ValueError(f"{chosen} nao e um executavel ELF Linux i386 valido")
        if not any(name.startswith(("lib/", "usr/lib/", "opt/wine/lib/")) for name in names):
            raise ValueError("ZIP sem bibliotecas Linux/Wine; um executavel isolado nao basta")
        if chosen == "bin/wine":
            # Preserve the original file and permissions.
            alias = False
        else:
            alias = True
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=".boxedwine-", suffix=".zip",
                                        dir=str(destination.parent))
        os.close(fd)
        try:
            with ZipFile(temporary, "w", allowZip64=True) as output:
                for member in archive.infolist():
                    if member.filename.startswith("/") or ".." in member.filename.split("/"):
                        raise ValueError("ZIP contem caminho inseguro")
                    if member.is_dir():
                        output.writestr(member, b"")
                        continue
                    with archive.open(member) as reader, output.open(member, "w") as writer:
                        while True:
                            data = reader.read(CHUNK)
                            if not data:
                                break
                            writer.write(data)
                if alias:
                    # This is a byte-for-byte copy of the genuine Wine ELF,
                    # NOT a stub or a renamed Windows DLL.
                    info = ZipInfo("bin/wine")
                    info.create_system = 3
                    info.external_attr = (stat.S_IFREG | 0o755) << 16
                    info.compress_type = ZIP_DEFLATED
                    with archive.open(chosen) as reader, output.open(info, "w") as writer:
                        while True:
                            data = reader.read(CHUNK)
                            if not data:
                                break
                            writer.write(data)
            with ZipFile(temporary) as check:
                with check.open("bin/wine") as reader:
                    if not elf32_i386(reader.read(64)):
                        raise ValueError("Falha ao validar bin/wine final")
            os.replace(temporary, destination)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    return chosen


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        source = prepare(args.input, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, f"ERRO: {error}\n")
    print(f"OK: Wine Linux i386 de {source}; /bin/wine verificado em {args.output}")
    print("Nota: a inicializacao real do Wine ainda precisa ser testada no BoxedWine.")


if __name__ == "__main__":
    main()
