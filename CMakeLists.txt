cmake_minimum_required(VERSION 3.20)
project(EmuWasm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EMUWASM_BUILD_EXTRACTED "Attempt to compile extracted BoxedWine CPU/MMU sources (experimental)" OFF)

add_library(emuwasm_probe STATIC src/probe.cpp)
target_include_directories(emuwasm_probe PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(EMUWASM_BUILD_EXTRACTED)
    file(GLOB_RECURSE BOXEDWINE_CPU CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/engine/emulation/cpu/common/*.cpp"
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
endif()
