# EmuWasm

Experimental Windows x86 emulation project targeting WebAssembly and WebGPU.
**This is not yet a Windows application runtime or a browser game launcher.**

## What actually works

- Original BoxedWine x86 interpreter and soft-MMU compile and link on Linux CI.
- Headless x86 regression executes arithmetic, a conditional loop and Linux i386
  `int 0x80` exit with the expected exit status.
- A portable PE32 (32-bit x86) loader parses MZ/PE/COFF headers, validates
  section ranges, expands file sections into a virtual image, zero-fills BSS,
  identifies the entry point and applies `IMAGE_REL_BASED_HIGHLOW` relocations.
- A deterministic synthetic Windows PE32 fixture is mapped into original
  BoxedWine guest memory and its `.text` instructions are executed. The test
  checks a rebased absolute pointer and the resulting x86 registers.
- PE32 import descriptors, named exports and ordinal imports are parsed into
  a symbol list. A guest-address callback binds the IAT transactionally;
  unresolved imports produce an explicit error without partial IAT writes.
- A second BoxedWine fixture executes an indirect x86 `CALL [IAT]` into a
  **synthetic guest function stub**, returning through a real guest stack.
  This tests import call mechanics, **not** the Windows API implementation.

## What does not work yet

- Actual Wine/Windows DLL implementations (`ntdll.dll`, `kernel32.dll`,
  etc.) and PE DLL export resolution. The import binder currently requires
  a caller-supplied guest address, not a host function pointer.
- Windows process environment (PEB/TEB), SEH, TLS, Windows system calls and
  startup conventions.
- Executing an arbitrary Windows `.exe` or Direct3D 11 game in the browser.
- A WebGPU translation backend.

The PE32 parser accepts import tables but **never executes Windows APIs
implicitly**. Missing DLL exports are rejected by the binding step.

## Wine and BoxedWine integration plan

**BoxedWine remains the x86 interpreter and guest-memory engine.** Wine is the
reference implementation for Windows loader behavior and APIs, especially its
`ntdll` PE loader and `kernel32` compatibility layer.

The current `src/pe32.cpp` is an independently written PE32 image-layout,
relocation and import-table implementation. **Wine code has not yet been
integrated or compiled into EmuWasm**. Wine cannot be directly linked into
the browser interpreter without adapting its native process assumptions,
host ABI, module loading and Windows/Unix boundary.

Next milestones:

1. Parse PE32 DLL export tables and load modules into separate guest ranges.
2. Connect the guest IAT resolver to actual Wine-compatible DLL exports,
   including forwarded exports and ordinal lookup.
3. Establish the Windows PEB/TEB, initial stack and process startup.
4. Add Win32 API coverage and a graphics translation layer.

## Build

Requires CMake 3.20+ and a C++17 compiler. The standalone PE32 parser
builds without the BoxedWine dependency:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The original BoxedWine interpreter build is exercised by the
[CPU source audit](.github/workflows/cpu-audit.yml) workflow, which checks
out upstream sources and compiles the CPU/kernel/platform dependency graph.
Its tests include `boxedwine_x86_execution`, `boxedwine_pe32_execution`,
`boxedwine_pe32_import_execution` and `pe32_parser`.

## Third-party licensing

BoxedWine and Wine are separate upstream projects with their own licenses.
Preserve all upstream copyright/license notices and review GPL/LGPL
obligations before redistributing a combined runtime.
