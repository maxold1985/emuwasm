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
- The PE32 loader now accepts **DLL images**, parses their export address
  tables (named and ordinal exports) and detects forwarded exports.
- A guest DLL registry resolves imports by module name (case-insensitive)
  and export name (case-sensitive) or ordinal, follows forwarded exports and
  rejects circular forwarding and overlapping guest DLL address ranges.
- An end-to-end BoxedWine fixture maps a PE32 EXE and a separate PE32 DLL,
  patches the EXE's IAT with the DLL's guest address, and executes an x86
  `CALL [IAT]` into the DLL before returning through the guest stack.
  The DLL contains **synthetic x86 code**, not Windows API functionality.

## What does not work yet

- Executing actual Wine DLL implementations (`ntdll.dll`, `kernel32.dll`,
  etc.). Real Wine PE DLLs can now be loaded from disk and their imports
  linked, but their Windows/Unix runtime dependencies are **not implemented**.
  Import binding alone does not make a Windows API callable.
- Windows process environment (PEB/TEB), SEH, TLS, Windows system calls and
  startup conventions.
- Executing an arbitrary Windows `.exe` or Direct3D 11 game in the browser.
- A WebGPU translation backend.

The PE32 parser accepts import tables but **never executes Windows APIs
implicitly**. Missing DLL exports are rejected by the binding step.

## Connect genuine Wine PE32 DLL files (linking only)

The `emuwasm_wine_link` tool loads **real on-disk Wine 32-bit PE DLL
files**, recursively follows their import dependencies, maps them into
separate guest virtual ranges, applies HIGHLOW base relocations and patches
the EXE and DLL import tables using their actual exported guest addresses.
It handles named, ordinal and forwarded exports and rejects missing modules
or exports with a diagnostic.

Use **Wine's `i386-windows` PE DLL directory**, not Wine's `i386-unix`
shared libraries, and not Microsoft's Windows system DLLs:

```sh
cmake -S . -B build
cmake --build build --target emuwasm_wine_link

./build/emuwasm_wine_link \\
  --wine-dir /usr/lib/i386-linux-gnu/wine/i386-windows \\
  path/to/program.exe
```

On Windows with Visual Studio 2022, extract Wine's **32-bit PE DLL files**
to a local directory, then run:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target emuwasm_wine_link
build\\Release\\emuwasm_wine_link.exe --wine-dir "D:\\wine\\i386-windows" "D:\\apps\\program.exe"
```

The DLL search is case-insensitive, supports multiple `--wine-dir` options
and does not call native Windows `LoadLibrary`. This loader does **not**
execute the EXE or DLLs: Wine PE exports may require its Unix-side libraries,
PEB/TEB, loader lists, syscall dispatch, TLS, initialization, API sets,
exception handling and proper Win32 process startup. Real Wine binaries
have **not yet been validated in CI**; current tests use deterministic
on-disk PE32 DLL fixtures. Unsupported PE features fail explicitly.

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

1. Validate the loader against real Wine i386-windows DLL binaries and
   map all DLL sections into BoxedWine's soft-MMU with correct page protections.
2. Establish Windows PEB/TEB, loader lists, TLS, stack and process startup.
3. Adapt Wine's Unix/Windows ABI boundary and syscall dispatch for BoxedWine.
4. Add Direct3D 11-to-WebGPU graphics translation.

## Build

Requires CMake 3.20+ and a C++17 compiler. The standalone PE32 parser
builds without the BoxedWine dependency:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Inspect an on-disk Windows PE32 executable or DLL (without running it):

```sh
./build/emuwasm_pe32_inspect --dll path/to/example.dll
./build/emuwasm_pe32_inspect --base 0x500000 path/to/example.exe
```

The inspector validates PE headers and prints section layouts, exports,
forwarded exports and unresolved imports. Unsupported TLS and delay-load
directories still produce an explicit error.

The original BoxedWine interpreter build is exercised by the
[CPU source audit](.github/workflows/cpu-audit.yml) workflow, which checks
out upstream sources and compiles the CPU/kernel/platform dependency graph.
Its tests include `boxedwine_x86_execution`, `boxedwine_pe32_execution`,
`boxedwine_pe32_import_execution`, `pe32_parser` and `pe32_modules`.

## Third-party licensing

BoxedWine and Wine are separate upstream projects with their own licenses.
Preserve all upstream copyright/license notices and review GPL/LGPL
obligations before redistributing a combined runtime.
