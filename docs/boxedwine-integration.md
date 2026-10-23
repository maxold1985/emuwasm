# BoxedWine source integration

The EmuWasm `engine/` directory is intended to hold original BoxedWine C++ source files, not rewritten stubs.

## Local import (PowerShell)

From the EmuWasm repository root, with BoxedWine already cloned in `../third_party/Boxedwine`:

```powershell
$source = Resolve-Path "..\third_party\Boxedwine\source"
New-Item -ItemType Directory -Force engine | Out-Null
Copy-Item "$source\emulation" engine -Recurse -Force
Copy-Item "$source\kernel" engine -Recurse -Force
Copy-Item "$source\util" engine -Recurse -Force
Copy-Item "$source\io" engine -Recurse -Force
```

If you already extracted selected files into a separate `wasmpc/engine`, copy those instead, and keep the upstream headers and license notices.

## Initial dependency audit

```powershell
Get-ChildItem engine -Recurse -Include *.cpp,*.h | Measure-Object
cmake -S . -B build-cpu -DEMUWASM_BUILD_EXTRACTED=ON
cmake --build build-cpu --parallel 4
```

The experimental build option attempts compilation of extracted CPU/MMU sources; it is not yet expected to link successfully without resolving additional upstream dependencies. Collect the first compiler diagnostic and locate its original upstream header or implementation. Do not create empty replacement headers.

The project currently does not execute EXE files.
