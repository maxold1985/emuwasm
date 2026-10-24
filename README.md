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
- An on-disk Wine-style DLL dependency loader and `emuwasm_wine_link`
  command now recursively load i386 PE DLL files, rebase modules and bind
  EXE/DLL IATs. A reusable BoxedWine adapter maps the linked images with
  guest section page protections. A separate CPU integration test executes
  an EXE -> kernel32 -> ntdll guest x86 call chain using **synthetic**
  PE DLL files written to disk.
- A minimal Windows x86 process skeleton builds guest **TEB, PEB,
  PEB_LDR_DATA, circular module lists and RTL_USER_PROCESS_PARAMETERS**
  using the offsets in Wine's `winternl.h`. The BoxedWine adapter installs
  the TEB as a genuine x86 **FS segment via KThread::setTLS/CPU::setSegment**.
  An interpreter regression checks guest `mov eax, fs:[0x30]`, followed by
  PEB.ImageBaseAddress, loader-list traversal and thread/process IDs.

## What does not work yet

- Executing actual Wine DLL implementations (`ntdll.dll`, `kernel32.dll`,
  etc.). Real Wine PE DLLs can now be loaded from disk and their imports
  linked, but their Windows/Unix runtime dependencies are **not implemented**.
  Import binding alone does not make a Windows API callable.
- A **complete** Windows process environment: only a minimal PEB/TEB and
  loader list are present. Wine's actual ntdll initialization, SEH, PE TLS
  callbacks, process heap, API sets, Unix-call dispatch, Windows syscalls
  and runtime startup conventions are not supported.
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
and does not call native Windows `LoadLibrary`. The
`mapLinkedWineImages()` adapter maps the linked EXE and DLL images into
BoxedWine's original guest soft-MMU with section page protections. This loader does **not**
execute the EXE or DLLs: Wine PE exports may require its Unix-side libraries,
PEB/TEB, loader lists, syscall dispatch, TLS, initialization, API sets,
exception handling and proper Win32 process startup. Real Wine binaries
have **not yet been validated in CI**; current tests use deterministic
on-disk PE32 DLL fixtures. Unsupported PE features fail explicitly.

## Prism3D: encerramento na reserva de 150 MiB (BoxedWine Windows)

Um log do 18 Wheels of Steel mostrou que o processo inicia sob Wine 11 como
Windows XP SP3, detecta DirectX 8.1, carrega as DLLs do jogo e chama
`NtAllocateVirtualMemory` com `RegionSize=0x09600000` (150 MiB). O ultimo
registro era `map_view got mem` seguido de `mprotect_exec forcing exec
permission` no intervalo `0x12e00000-0x1c3fffff`. Isso **nao comprova**
falha de alocacao; o log termina antes do retorno da API.

O build original BoxedWine 26R1 nao aceita `-disableLinearMemory`. Para
testar a implementacao atualizada, baixe o artifact
`boxedwine-full-runtime-windows-x64` do
[workflow de compilacao Windows do codigo master](.github/workflows/boxedwine-windows.yml)
e abra `run_prism3d_memory.bat`. Ele executa o Wine em modo XP com
`-disableLinearMemory` (MMU legada) e `WINEDEBUG=+seh,+loaddll`.

Use `run_prism3d_winxp.bat` para comparar a memoria padrao do mesmo
build; adicione `-NoSound` para testar o audio, ou
`-TraceVirtualMemory` para registro extremamente detalhado.
Logs disponiveis em `%LOCALAPPDATA%\EmuWasm\Logs`:
`launcher-*.log`, `prism3d-*.log`, `wine-set-winxp.log`.

Este teste de MMU e **experimental**, nao uma correcao confirmada:
o jogo real nao e testado na CI. A versao 26R1 tem um lancador
sem suporte a `-disableLinearMemory`.

## Windows 10 x64: executable nativo (sem WSL)

O executavel `boxedwine` publicado pelo workflow Linux **nao roda no Windows**.

Para usar imediatamente um EXE Windows x64 verificado, o workflow
[Windows BoxedWine 26R1 verified upstream package](.github/workflows/boxedwine-windows-upstream.yml)
baixa a release oficial 26R1.0, verifica o SHA-256 informado pela API de
releases do GitHub e inclui `Boxedwine.exe`, `run_prism3d.bat` e
`run_prism3d.ps1` no artifact `boxedwine-26R1-upstream-windows-x64`.
Esse pacote **usa binarios oficiais prontos**, nao substitui a compilacao do codigo-fonte
no workflow `boxedwine-windows.yml`. Os arquivos de jogo e o filesystem Wine
completo continuam sendo fornecidos separadamente.

E os arquivos `.wasm` e `.js` so executam no navegador, servidos por HTTP(S).

Para Windows 10 x64, use [Full BoxedWine Windows x64 runtime](.github/workflows/boxedwine-windows.yml),
que compila o BoxedWine original como um EXE PE32+ AMD64 e empacota as DLLs.
A compilacao no GitHub Actions precisa concluir com sucesso antes de baixar
o artifact `boxedwine-full-runtime-windows-x64`.

1. Extraia **todos os arquivos** do artifact na mesma pasta.
2. Execute `Boxedwine.exe` para abrir o gerenciador grafico e obter o
   filesystem Linux/Wine completo do BoxedWine, se ainda nao o tiver.
3. Use `run_prism3d.bat` para iniciar o jogo instalado em
   `C:\GOG Games\18 Wheels of Steel Pedal to the Metal`.
4. Caso tenha instalado o jogo em outro diretorio, no PowerShell:

~~~powershell
powershell -ExecutionPolicy Bypass -File .\run_prism3d.ps1 -GameDirectory "D:\Jogos\18 Wheels of Steel" -WineRootZip "C:\caminho\Wine.zip"
~~~

O script monta a instalacao no drive D: convidado usando `-mount_drive`,
cria um perfil em %LOCALAPPDATA%/EmuWasm/Prism3D e executa
`/bin/wine D:\prism3d.exe`. Nao copie apenas as DLLs PE32 do Wine:
o sistema de arquivos completo contem tambem o Wine/Linux convidado.

O backend grafico e o SDL/OpenGL do BoxedWine original, **nao** um conversor
DirectX 11 para WebGPU. O sucesso da compilacao do EXE nao comprova que
o jogo Prism3D chegara ao menu principal ou desenhara 3D corretamente.

## Running a real Windows game through the complete BoxedWine Wine runtime

**Recommended runtime path.** Original [BoxedWine](https://github.com/danoon2/Boxedwine)
already emulates Linux i386 running Wine, including CPU, filesystem,
graphics and sound services. This is substantially more complete than trying
to execute the PE export addresses in genuine kernel32.dll directly through
the standalone PE32 linker.

The [Full BoxedWine runtime workflow](.github/workflows/boxedwine-full-runtime.yml)
compiles unmodified upstream BoxedWine for Linux (WSL2 compatible).
Choose **Run workflow** and set the optional build_wasm input to true to
also compile its larger browser release. Download the relevant artifact after
a successful run.

### Linux/WSL2: try the original full Wine runtime

You need (1) your installed game directory, (2) the
'boxedwine-full-runtime-linux-x86_64' binary artifact, and (3) a complete
compatible Linux/Wine filesystem ZIP downloaded using the BoxedWine
filesystem manager, e.g. TinyCore15Wine11.0.zip. The existing
'wine/i386-windows' folder of PE DLLs alone is insufficient. To obtain a
compatible root ZIP, run the original Linux BoxedWine binary without
arguments, then use its filesystem download dialog. Upstream documents the
Linux filesystem directory as ~/.local/share/Boxedwine/FileSystems2.
For running a downloaded binary from CI, make sure it is executable first.

Run these commands inside WSL2 Ubuntu, substituting the actual paths
to the BoxedWine binary and filesystem ZIP:

~~~bash
chmod +x ./boxedwine
python3 scripts/run_boxedwine_game.py \
  --boxedwine ./boxedwine \
  --wine-root-zip /mnt/c/Users/max/Downloads/TinyCore15Wine11.0.zip \
  --game-dir "/mnt/c/GOG Games/18 Wheels of Steel Pedal to the Metal" \
  --exe prism3d.exe \
  --profile ./boxedwine-game-profile \
  --resolution 1024x768
~~~

Add --dry-run to validate paths and print the command without executing.
BoxedWine's -mount maps the entire game installation into the emulated
Linux filesystem, so Prism3D can access its assets. You need a working
X11/WSLg display. A launched Wine process is not proof of game compatibility;
inspect the SDL window, console output and Wine diagnostics.

### Browser port: 18 Wheels of Steel launcher (WASM + WebGL)

O projeto inclui agora [`browser_port/`](browser_port) com uma pagina em
portugues, servidor HTTP privado em Python 3, carregamento dos seus
arquivos ZIP pela propria pagina, selecao de renderizador do Wine e
o runtime integral do BoxedWine WebAssembly. Nao e um port nativo do
motor Prism3D e nao inclui arquivos proprietarios do jogo.

1. Acesse o workflow
   [`18 Wheels of Steel browser port`](.github/workflows/18wos-browser-port.yml)
   e baixe o artifact `emuwasm-18wos-browser-webgl-wasm` apos a CI passar.
2. Extraia o ZIP e execute `start_windows.bat` no Windows 10 (Python 3).
   O servidor abre apenas em `http://127.0.0.1:8000/`.
3. Na pagina web, importe um ZIP completo do Wine/BoxedWine, por exemplo
   o `TinyCore15Wine11.0.zip` que voce ja baixou.
4. Gere **localmente** o ZIP com os arquivos originais do jogo:

   ```powershell
   py -3 package_boxedwine_game.py --game-dir "C:\GOG Games\18 Wheels of Steel Pedal to the Metal" --exe prism3d.exe --output prism3d.zip
   ```

5. Importe `prism3d.zip` pelo navegador e clique em **Iniciar 18 Wheels
   of Steel**. Por padrao, a configuracao aplica Wine Windows XP,
   OpenGL/WebGL, 1024x768 e audio desativado.

Os ZIPs ficam em `.local-game-data/` e nunca sao publicados pelo Actions.
O servidor inclui COOP/COEP e verifica os caminhos internos dos ZIPs
antes de servi-los. O perfil do Wine no navegador e separado e
persistido via IndexedDB, conforme o suporte do BoxedWine.

**Limitacao:** nenhuma imagem 3D renderizada do jogo real foi validada.
O log mais recente de Windows foi ate `Showing Window` e terminou com
`Vulkan not compiled into Boxedwine`. A interface browser evita selecionar
Vulkan e oferece OpenGL/WebGL ou GDI para diagnostico, mas o upstream
pode ainda falhar ao criar os dispositivos DirectDraw/Direct3D.

### Browser: package your original game files

After setting build_wasm=true and downloading the browser artifact, put
all its files in a web directory. Also put a compatible BoxedWine
Linux/Wine root filesystem ZIP named boxedwine.zip there. This is NOT
the i386-windows PE DLL directory from the import-linking test.

From Windows PowerShell:

~~~powershell
py -3 scripts/package_boxedwine_game.py --game-dir "C:\GOG Games\18 Wheels of Steel Pedal to the Metal" --exe prism3d.exe --output prism3d.zip
~~~

Copy prism3d.zip alongside boxedwine.html, boxedwine.js, boxedwine.wasm,
the other web assets, and boxedwine.zip. Serve the directory from a local
web server, then open:

~~~text
http://localhost:8000/boxedwine.html?app=prism3d&p=prism3d.exe
~~~

Do not open the page through file://. For multithreaded browser builds,
set COOP/COEP headers; upstream project/emscripten/server.mjs supports them.
Game files are never downloaded or included by this project's GitHub Actions.
Keep game ZIP archives on a private local server to avoid unauthorized
redistribution of copyrighted game assets.

Original BoxedWine uses a WebGL graphics backend in the browser, which may
not support all DirectDraw/Direct3D operations required by Prism3D.
Browser compatibility, performance and reaching a playable game state are
unverified. The emuwasm DX11-to-WebGPU backend is not implemented yet.

## Headless execution probe for real PE32 programs (Linux/WSL2)

**Experimental:** `emuwasm_pe32_run` now advances genuine guest x86 instructions
from a supplied EXE using the original BoxedWine CPU, after mapping Wine PE
DLLs and installing the project's minimal PEB/TEB/FS state. It stops **before
the first DLL instruction** by default, since genuine Wine DLL initialization
and its Unix-side ABI are not implemented. It **does not start a playable game**,
open a window, or translate graphics to WebGPU.

Build it on Linux/WSL2 with BoxedWine sources staged via the
[BoxedWine CPU workflow](.github/workflows/boxedwine-cpu.yml); a passing workflow
publishes the Linux executable in the `emuwasm-pe32-run-linux-x86_64` artifact.
This target is **not part of the current Node.js WASM bundle**.

To probe the same game files from a WSL2 terminal, after downloading the
workflow's Linux executable to the current directory:

```sh
chmod +x ./emuwasm_pe32_run
./emuwasm_pe32_run \
  --no-game-dir \
  --game-dir "/mnt/c/Users/max/Downloads/roblox_backup/prism3d-dll-test" \
  --wine-dir "/mnt/c/Users/max/Downloads/roblox_backup/emuwasm-wasm-node/wine/i386-windows" \
  --max-steps 10000 \
  "/mnt/c/GOG Games/18 Wheels of Steel Pedal to the Metal/prism3d.exe"
```

`--trace` prints x86 EIP and registers for every step. Use
`--allow-dll-steps` **only for debugging**, not for playing games:
Wine's real DLL startup and Unix calls are missing and it may crash.
An expected diagnostic result looks like `STOP: reached <dll> ...`
or `STOP: --max-steps=... reached`; neither means the game launched.

The CI test `boxedwine_pe32_runner_cli` uses self-generated PE32 images and
checks that an actual x86 `CALL [IAT]` moves execution from the EXE to
a mapped DLL boundary. No game executables are included in the repository.

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
2. Complete Wine's Windows PEB/TEB process state, DLL initialization
   and PE TLS. Current PEB/TEB/loader lists are a tested partial skeleton.
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
`boxedwine_pe32_import_execution`, `boxedwine_wine_dll_execution`,
`boxedwine_win32_fs_execution`, `pe32_parser`, `pe32_modules`,
`pe32_wine` and `pe32_process`.

## Third-party licensing

BoxedWine and Wine are separate upstream projects with their own licenses.
Preserve all upstream copyright/license notices and review GPL/LGPL
obligations before redistributing a combined runtime.
