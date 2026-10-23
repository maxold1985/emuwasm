# EmuWasm

Experimental Windows x86 emulation in the browser using C++ and WebAssembly.

## Status

Stage 1: repository scaffolding. No EXE execution yet.

## Source integration

The original BoxedWine sources are not included in this repository yet. The `engine/` directory will contain locally extracted sources. Compilation of the original BoxedWine CPU requires additional source dependencies and platform configuration.

## Build

Requires CMake 3.20+ and a C++17 compiler. Initially builds a small project configuration probe, **not** a functional emulator.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To configure using Emscripten:

```sh
emcmake cmake -S . -B build-wasm
cmake --build build-wasm
```

## Third-party licensing

BoxedWine is a separate upstream project. Preserve its copyright and license notices when incorporating its source files, and verify the applicable GPL obligations before distribution.
