cmake_minimum_required(VERSION 3.20)
project(EmuWasm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EMUWASM_BUILD_X86_TEST "Build real BoxedWine x86 execution test (requires full upstream runtime)" OFF)
option(EMUWASM_BUILD_KERNEL_OBJECTS "Compile original BoxedWine kernel implementation objects (dependency audit)" OFF)
option(EMUWASM_BUILD_EXTRACTED "Attempt to compile extracted BoxedWine CPU/MMU sources (experimental)" OFF)

add_library(emuwasm_probe STATIC src/probe.cpp)
target_include_directories(emuwasm_probe PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Portable PE32 parser/relocator: does not depend on BoxedWine or Wine.
add_library(emuwasm_pe32 STATIC src/pe32.cpp src/pe32_modules.cpp src/pe32_wine.cpp src/pe32_process.cpp)
target_include_directories(emuwasm_pe32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# Inspect real on-disk PE32 images without executing guest code.
add_executable(emuwasm_pe32_inspect tools/pe32_inspect.cpp)
target_link_libraries(emuwasm_pe32_inspect PRIVATE emuwasm_pe32)

# Read Wine's genuine i386-windows PE DLLs from disk and bind guest IATs.
add_executable(emuwasm_wine_link tools/wine_link.cpp)
target_link_libraries(emuwasm_wine_link PRIVATE emuwasm_pe32)

if(EMUWASM_BUILD_EXTRACTED)
    file(GLOB_RECURSE BOXEDWINE_CPU CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/common/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/decoder.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/normal/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/softmmu/*.cpp")
    if(NOT BOXEDWINE_CPU)
        message(FATAL_ERROR "No extracted CPU/MMU sources found in engine/")
    endif()
    add_library(emuwasm_boxedwine STATIC ${BOXEDWINE_CPU})
    # Upstream BoxedWine uses C++20 concepts and requires expressions.
    target_compile_features(emuwasm_boxedwine PRIVATE cxx_std_20)
    target_include_directories(emuwasm_boxedwine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/engine"
        "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/lib/simde"
        "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform"
        "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/common"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/normal"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/softmmu"
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/kernel"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/boxedwine_include")
    if(EMUWASM_BUILD_KERNEL_OBJECTS)
        # Compile upstream kernel objects without linking a runnable executable yet.
        # These files depend on the larger BoxedWine runtime (FS, scheduler, signals).
        file(GLOB BOXEDWINE_KERNEL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/engine/kernel/*.cpp")
        if(NOT BOXEDWINE_KERNEL_SOURCES)
            message(FATAL_ERROR "No original BoxedWine kernel sources found")
        endif()
        add_library(emuwasm_boxedwine_kernel OBJECT ${BOXEDWINE_KERNEL_SOURCES})
        target_compile_features(emuwasm_boxedwine_kernel PRIVATE cxx_std_20)
        target_include_directories(emuwasm_boxedwine_kernel PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
    endif()

    if(EMUWASM_BUILD_X86_TEST)
        # Compile the upstream utility implementations required by kernel/CPU
        # (BString, synchronization, logging, file helpers). No replacements.
        file(GLOB BOXEDWINE_UTIL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/engine/util/*.cpp")
        find_package(CURL REQUIRED)
        add_library(emuwasm_boxedwine_util STATIC ${BOXEDWINE_UTIL_SOURCES})
        target_link_libraries(emuwasm_boxedwine_util PUBLIC CURL::libcurl)
        # Original filesystem and procfs implementations referenced by KProcess.
        file(GLOB BOXEDWINE_IO_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/engine/io/*.cpp")
        file(GLOB BOXEDWINE_PROC_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/engine/kernel/proc/*.cpp")
        file(GLOB BOXEDWINE_LOADER_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/engine/kernel/loader/*.cpp")
        add_library(emuwasm_boxedwine_io STATIC
            ${BOXEDWINE_IO_SOURCES} ${BOXEDWINE_PROC_SOURCES} ${BOXEDWINE_LOADER_SOURCES})
        target_compile_features(emuwasm_boxedwine_io PRIVATE cxx_std_20)
        target_include_directories(emuwasm_boxedwine_io PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_compile_features(emuwasm_boxedwine_util PRIVATE cxx_std_20)
        target_include_directories(emuwasm_boxedwine_util PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        # The BoxedWine SoftFloat fork contains a reduced selection of C
        # files, while its inherited Makefile names missing upstream objects.
        # Compile the files that ACTUALLY exist, selecting ONLY 8086-SSE.
        set(BOXEDWINE_SOFTFLOAT_ROOT
            "${CMAKE_CURRENT_SOURCE_DIR}/lib/softfloat")
        file(GLOB BOXEDWINE_SOFTFLOAT_CORE CONFIGURE_DEPENDS
            "${BOXEDWINE_SOFTFLOAT_ROOT}/source/*.c")
        file(GLOB BOXEDWINE_SOFTFLOAT_X86 CONFIGURE_DEPENDS
            "${BOXEDWINE_SOFTFLOAT_ROOT}/source/8086-SSE/*.c")
        add_library(emuwasm_softfloat STATIC
            ${BOXEDWINE_SOFTFLOAT_CORE} ${BOXEDWINE_SOFTFLOAT_X86})
        # Retain BoxedWine's customized platform.h: it defines thread-local
        # SoftFloat state and intentionally leaves INLINE_LEVEL undefined.
        target_include_directories(emuwasm_softfloat PRIVATE
            "${BOXEDWINE_SOFTFLOAT_ROOT}/source/include"
            "${BOXEDWINE_SOFTFLOAT_ROOT}/source/8086-SSE")
        target_compile_definitions(emuwasm_softfloat PRIVATE
            SOFTFLOAT_FAST_INT64 SOFTFLOAT_ROUND_ODD
            SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32)

        # Native Linux platform and SDL host services from upstream sources.
        # These retain the original relative include paths under platform/.
        find_package(SDL2 CONFIG REQUIRED)
        find_package(Threads REQUIRED)
        add_library(emuwasm_boxedwine_platform STATIC
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/linux/platform.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/linux/platformThreads.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/knativesystem.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/knativethread.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/knativescreenSDL.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/knativeinputSDL.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/sdlcallback.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl/kdspaudio.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/kernel/devs/devinput.cpp"
            # Upstream BoxedWine now calls CD-ROM helpers from kprocess/SDL.
            # Link their ORIGINAL implementation instead of adding dummy stubs.
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/kernel/devs/devcdrom.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/sdl/singleThreaded/mainloop.cpp")
        target_compile_features(emuwasm_boxedwine_platform PRIVATE cxx_std_20)
        # The upstream SDL host defines its own main() unless built for tests.
        target_compile_definitions(emuwasm_boxedwine_platform PRIVATE __TEST)
        target_include_directories(emuwasm_boxedwine_platform PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/linux"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/x11")
        target_link_libraries(emuwasm_boxedwine_platform PUBLIC SDL2::SDL2 Threads::Threads)

        # The headless interpreter still links X11 and GL dispatch symbols
        # because the original CPU and kernel reference them.
        file(GLOB BOXEDWINE_X11_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/x11/*.cpp")
        file(GLOB BOXEDWINE_GL_SOURCES CONFIGURE_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/opengl/*.cpp")
        add_library(emuwasm_boxedwine_display STATIC
            ${BOXEDWINE_X11_SOURCES} ${BOXEDWINE_GL_SOURCES})
        target_compile_features(emuwasm_boxedwine_display PRIVATE cxx_std_20)
        target_include_directories(emuwasm_boxedwine_display PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/platform/sdl"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/opengl"
            "${CMAKE_CURRENT_SOURCE_DIR}/boxedwine-upstream/source/x11")
        target_compile_definitions(emuwasm_boxedwine_display PRIVATE GLH=<SDL_opengl.h>)
        target_link_libraries(emuwasm_boxedwine_display PUBLIC SDL2::SDL2)
        if(NOT EMUWASM_BUILD_KERNEL_OBJECTS)
            message(FATAL_ERROR "EMUWASM_BUILD_X86_TEST requires EMUWASM_BUILD_KERNEL_OBJECTS=ON")
        endif()
        add_executable(emuwasm_x86_execution tests/boxedwine_x86_execution.cpp)
        target_compile_features(emuwasm_x86_execution PRIVATE cxx_std_20)
        target_include_directories(emuwasm_x86_execution PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        # Group static archives to resolve circular references among the
        # original CPU/kernel/filesystem/platform/graphics runtime libraries.
        target_link_libraries(emuwasm_x86_execution PRIVATE
            "-Wl,--start-group"
            emuwasm_boxedwine emuwasm_boxedwine_kernel
            emuwasm_boxedwine_util emuwasm_boxedwine_io
            emuwasm_boxedwine_platform emuwasm_boxedwine_display emuwasm_softfloat
            "-Wl,--end-group")
        # Run a real rebased Windows PE32 image through the same interpreter.
        add_executable(emuwasm_pe32_execution tests/boxedwine_pe32_execution.cpp)
        target_compile_features(emuwasm_pe32_execution PRIVATE cxx_std_20)
        target_include_directories(emuwasm_pe32_execution PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_link_libraries(emuwasm_pe32_execution PRIVATE
            emuwasm_pe32
            "-Wl,--start-group"
            emuwasm_boxedwine emuwasm_boxedwine_kernel
            emuwasm_boxedwine_util emuwasm_boxedwine_io
            emuwasm_boxedwine_platform emuwasm_boxedwine_display emuwasm_softfloat
            "-Wl,--end-group")
        # Resolve one PE32 import to a guest stub and execute CALL [IAT].
        add_executable(emuwasm_pe32_import_execution tests/boxedwine_pe32_import_execution.cpp)
        target_compile_features(emuwasm_pe32_import_execution PRIVATE cxx_std_20)
        target_include_directories(emuwasm_pe32_import_execution PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_link_libraries(emuwasm_pe32_import_execution PRIVATE
            emuwasm_pe32
            "-Wl,--start-group"
            emuwasm_boxedwine emuwasm_boxedwine_kernel
            emuwasm_boxedwine_util emuwasm_boxedwine_io
            emuwasm_boxedwine_platform emuwasm_boxedwine_display emuwasm_softfloat
            "-Wl,--end-group")
        # Reusable production adapter from PE image layout to BoxedWine soft-MMU.
        add_library(emuwasm_pe32_boxedwine STATIC src/pe32_boxedwine.cpp)
        target_compile_features(emuwasm_pe32_boxedwine PRIVATE cxx_std_20)
        target_include_directories(emuwasm_pe32_boxedwine PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_link_libraries(emuwasm_pe32_boxedwine PUBLIC emuwasm_pe32)
        # Load a PE32 EXE and two DLLs from disk, link their imports, then
        # execute EXE -> kernel32 -> ntdll using the original BoxedWine CPU.
        add_executable(emuwasm_boxedwine_wine_dll_execution tests/boxedwine_wine_dll_execution.cpp)
        target_compile_features(emuwasm_boxedwine_wine_dll_execution PRIVATE cxx_std_20)
        target_include_directories(emuwasm_boxedwine_wine_dll_execution PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_link_libraries(emuwasm_boxedwine_wine_dll_execution PRIVATE
            emuwasm_pe32_boxedwine emuwasm_pe32
            "-Wl,--start-group"
            emuwasm_boxedwine emuwasm_boxedwine_kernel
            emuwasm_boxedwine_util emuwasm_boxedwine_io
            emuwasm_boxedwine_platform emuwasm_boxedwine_display emuwasm_softfloat
            "-Wl,--end-group")
        # Original x86 CPU reads FS:[0x30] to traverse Wine-style TEB, PEB,
        # module lists and thread-local process metadata.
        add_executable(emuwasm_win32_fs_execution tests/boxedwine_win32_fs_execution.cpp)
        target_compile_features(emuwasm_win32_fs_execution PRIVATE cxx_std_20)
        target_include_directories(emuwasm_win32_fs_execution PRIVATE
            $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
        target_link_libraries(emuwasm_win32_fs_execution PRIVATE
            emuwasm_pe32_boxedwine emuwasm_pe32
            "-Wl,--start-group"
            emuwasm_boxedwine emuwasm_boxedwine_kernel
            emuwasm_boxedwine_util emuwasm_boxedwine_io
            emuwasm_boxedwine_platform emuwasm_boxedwine_display emuwasm_softfloat
            "-Wl,--end-group")
        if(NOT EMSCRIPTEN)
            add_test(NAME boxedwine_win32_fs_execution COMMAND emuwasm_win32_fs_execution)
            add_test(NAME boxedwine_x86_execution COMMAND emuwasm_x86_execution)
            add_test(NAME boxedwine_pe32_execution COMMAND emuwasm_pe32_execution)
            add_test(NAME boxedwine_pe32_import_execution COMMAND emuwasm_pe32_import_execution)
            add_test(NAME boxedwine_wine_dll_execution COMMAND emuwasm_boxedwine_wine_dll_execution)
        endif()
    endif()

    # Compile the real upstream CPU API contract as a separate object.
    # An object target avoids falsely claiming a runnable interpreter before
    # KMemory, KThread and their runtime dependencies are linked.
    add_library(emuwasm_cpu_api_check OBJECT tests/boxedwine_cpu_api.cpp)
    target_compile_features(emuwasm_cpu_api_check PRIVATE cxx_std_20)
    target_include_directories(emuwasm_cpu_api_check PRIVATE
        $<TARGET_PROPERTY:emuwasm_boxedwine,INCLUDE_DIRECTORIES>)
    message(WARNING "Extracted BoxedWine build is experimental; upstream dependencies may be missing")
endif()

if(NOT EMSCRIPTEN)
    enable_testing()
    add_executable(emuwasm_smoke tests/smoke.cpp)
    target_link_libraries(emuwasm_smoke PRIVATE emuwasm_probe)
    add_test(NAME probe COMMAND emuwasm_smoke)
    add_executable(emuwasm_pe32_parser_test tests/pe32_loader.cpp)
    target_link_libraries(emuwasm_pe32_parser_test PRIVATE emuwasm_pe32)
    add_test(NAME pe32_parser COMMAND emuwasm_pe32_parser_test)
    add_executable(emuwasm_pe32_modules_test tests/pe32_modules.cpp)
    target_link_libraries(emuwasm_pe32_modules_test PRIVATE emuwasm_pe32)
    add_test(NAME pe32_modules COMMAND emuwasm_pe32_modules_test)
    add_executable(emuwasm_pe32_wine_test tests/pe32_wine.cpp)
    target_link_libraries(emuwasm_pe32_wine_test PRIVATE emuwasm_pe32)
    add_test(NAME pe32_wine COMMAND emuwasm_pe32_wine_test)
    add_executable(emuwasm_pe32_process_test tests/pe32_process.cpp)
    target_link_libraries(emuwasm_pe32_process_test PRIVATE emuwasm_pe32)
    add_test(NAME pe32_process COMMAND emuwasm_pe32_process_test)
endif()
