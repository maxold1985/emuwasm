#!/usr/bin/env python3
"""Audit PE32 game modules and transitive DLL dependencies without executing code.

Usage:
  python scripts/audit_game_pe.py "C:/GOG Games/18 Wheels of Steel Pedal to the Metal/prism3d.exe"
"""
import argparse
import json
import struct
from pathlib import Path

SYSTEM_DLLS = {
    "kernel32.dll", "user32.dll", "gdi32.dll", "ole32.dll",
    "wsock32.dll", "winmm.dll", "ddraw.dll", "advapi32.dll",
    "ntdll.dll", "shell32.dll", "comdlg32.dll", "comctl32.dll",
    "version.dll", "imm32.dll", "winspool.drv", "oleaut32.dll",
    "rpcrt4.dll", "ws2_32.dll", "dsound.dll", "dinput.dll",
    "dinput8.dll", "opengl32.dll", "glu32.dll", "d3d9.dll",
    "d3d11.dll", "dxgi.dll", "msvcr71.dll",
}


def inspect_pe(path):
    data = path.read_bytes()
    def u16(off):
        if off < 0 or off + 2 > len(data):
            raise ValueError("Truncated PE")
        return struct.unpack_from("<H", data, off)[0]
    def u32(off):
        if off < 0 or off + 4 > len(data):
            raise ValueError("Truncated PE")
        return struct.unpack_from("<I", data, off)[0]
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError("Not an MZ executable")
    pe = u32(0x3c)
    if data[pe:pe + 4] != b"PE\\0\\0":
        raise ValueError("Missing PE signature")
    machine, sections = u16(pe + 4), u16(pe + 6)
    optional = pe + 24
    opt_size = u16(pe + 20)
    if machine != 0x14c or u16(optional) != 0x10b:
        raise ValueError("Only PE32 i386 is supported")
    if opt_size < 104:
        raise ValueError("Optional header too short")
    image_base = u32(optional + 28)
    size_headers = u32(optional + 60)
    import_rva, import_size = u32(optional + 104), u32(optional + 108)
    sec_start = optional + opt_size
    ranges = []
    for i in range(sections):
        s = sec_start + i * 40
        if s + 40 > len(data):
            raise ValueError("Truncated section table")
        virtual_size, va, raw_size, raw_ptr = (u32(s + x) for x in (8, 12, 16, 20))
        ranges.append((va, max(virtual_size, raw_size), raw_ptr, raw_size))
    def rva_offset(rva, length=1):
        if rva < size_headers and rva + length <= len(data):
            return rva
        for va, span, raw, raw_size in ranges:
            if va <= rva and rva + length <= va + min(span, raw_size):
                pos = raw + rva - va
                if pos + length <= len(data):
                    return pos
        raise ValueError(f"Unmapped RVA 0x{rva:x}")
    def cstring(rva):
        pos = rva_offset(rva)
        end = data.find(b"\\0", pos, min(len(data), pos + 4096))
        if end < 0:
            raise ValueError("Unterminated import name")
        return data[pos:end].decode("ascii", errors="replace")
    imports = {}
    if import_rva:
        if import_size < 20:
            raise ValueError("Invalid import directory")
        for i in range(min(import_size // 20, 4096)):
            desc = rva_offset(import_rva + i * 20, 20)
            oft, _, _, name_rva, first_thunk = struct.unpack_from("<IIIII", data, desc)
            if not any((oft, name_rva, first_thunk)):
                break
            name = cstring(name_rva).lower()
            thunk = oft or first_thunk
            symbols = []
            for j in range(65536):
                entry = u32(rva_offset(thunk + j * 4, 4))
                if not entry:
                    break
                if entry & 0x80000000:
                    symbols.append(f"#{entry & 0xffff}")
                else:
                    symbols.append(cstring(entry + 2))
            imports[name] = symbols
    return {"image_base": image_base, "imports": imports}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("exe", type=Path, help="PE32 executable to audit")
    ap.add_argument("--json", type=Path, help="Write dependency report JSON")
    args = ap.parse_args()
    root = args.exe.resolve()
    if not root.is_file():
        ap.error(f"File not found: {root}")
    local = {p.name.lower(): p for p in root.parent.iterdir()
             if p.is_file() and p.suffix.lower() == ".dll"}
    pending = [root]
    visited = set()
    modules = []
    missing = set()
    while pending:
        path = pending.pop(0)
        key = path.name.lower()
        if key in visited:
            continue
        visited.add(key)
        try:
            info = inspect_pe(path)
        except (OSError, ValueError, struct.error) as exc:
            modules.append({"module": path.name, "error": str(exc)})
            continue
        deps = []
        for dll, symbols in info["imports"].items():
            target = local.get(dll)
            category = ("local" if target else
                        "system-runtime" if dll in SYSTEM_DLLS else "unresolved")
            deps.append({"dll": dll, "category": category,
                         "imports": symbols, "count": len(symbols)})
            if target:
                pending.append(target)
            elif category == "unresolved":
                missing.add(dll)
        modules.append({"module": path.name, "image_base": info["image_base"],
                        "dependencies": deps})
    report = {"entry": root.name, "modules": modules,
              "unresolved": sorted(missing),
              "note": "Static imports only; LoadLibrary/delay imports are not audited. "
                      "System-runtime classification does not mean API implementation exists."}
    for mod in modules:
        print(f"\\n{mod['module']}")
        if "error" in mod:
            print(f"  ERROR: {mod['error']}")
            continue
        for dep in mod["dependencies"]:
            print(f"  {dep['dll']:<24} {dep['category']:<15} {dep['count']} imports")
    if missing:
        print("\\nUnresolved:", ", ".join(sorted(missing)))
    if args.json:
        args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
        print(f"\\nReport: {args.json}")
    return 1 if any("error" in m for m in modules) else 0


if __name__ == "__main__":
    raise SystemExit(main())
