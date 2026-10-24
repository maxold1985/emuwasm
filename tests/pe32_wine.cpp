#include "emuwasm/pe32_wine.h"
#include "pe32_fixture.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
void save(const std::filesystem::path& file,
          const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}
std::uint32_t read32(const std::vector<std::uint8_t>& b, std::size_t offset) {
    return static_cast<std::uint32_t>(b.at(offset)) |
           (static_cast<std::uint32_t>(b.at(offset + 1)) << 8) |
           (static_cast<std::uint32_t>(b.at(offset + 2)) << 16) |
           (static_cast<std::uint32_t>(b.at(offset + 3)) << 24);
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    using namespace emuwasm::pe32;

    // On-disk fixture is a PE32 DLL with the same headers/export structures
    // as a Wine PE DLL, but its guest x86 function is a test double.
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("emuwasm-wine-loader-" + std::to_string(nonce));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        std::fprintf(stderr, "Cannot create fixture directory\n");
        return 2;
    }
    save(root / "app.exe", emuwasm::test::makePe32WithImports());
    save(root / "KERNEL32.DLL", emuwasm::test::makePe32Dll(
        "kernel32.dll", "GetTickCount", "ntdll.RtlGetTickCount"));
    save(root / "NTDLL.DLL", emuwasm::test::makePe32Dll(
        "ntdll.dll", "RtlGetTickCount"));

    std::string error;
    WineDllLoader wine;
    wine.addSearchDirectory(root);
    Image exe;
    check(wine.loadExecutable(root / "app.exe", exe, error),
          "read PE32 executable from disk");
    check(wine.linkExecutable(exe, error),
          "load Wine-style DLL files and resolve forwarded imports");
    const auto names = wine.modules().dllNames();
    check(names.size() == 2, "loaded kernel32 and ntdll dependencies");
    const Image* kernel = wine.modules().findDll("kernel32.dll");
    const Image* ntdll = wine.modules().findDll("ntdll.dll");
    check(kernel && ntdll, "case-insensitive on-disk DLL discovery");
    if (kernel && ntdll) {
        check(kernel->loadBase != ntdll->loadBase,
              "DLL images mapped to separate guest address ranges");
        check(ntdll->loadBase == 0x60010000,
              "relocate second DLL when preferred base overlaps first DLL");
        check(read32(exe.bytes, 0x2050) == ntdll->loadBase + 0x1000,
              "EXE IAT resolves forwarded export to rebased guest DLL");
        check(exe.loadBase == 0x400000,
              "EXE keeps its preferred guest load address");
    }
    check(!wine.loadDll("../kernel32.dll", error),
          "reject path traversal in DLL name");

    WineDllLoader missing;
    missing.addSearchDirectory(root);
    check(!missing.loadDll("user32.dll", error) &&
          error.find("missing Wine i386 PE DLL") != std::string::npos,
          "missing Wine dependency reports its module name");

    // Ensure that an unresolved export never writes to the EXE's IAT.
    auto wrongDll = emuwasm::test::makePe32Dll(
        "kernel32.dll", "SomeOtherFunction");
    save(root / "kernel32.dll", wrongDll); // direct spelling takes precedence
    WineDllLoader unresolved;
    unresolved.addSearchDirectory(root);
    Image fresh;
    check(unresolved.loadExecutable(root / "app.exe", fresh, error),
          "load EXE for unresolved-export test");
    check(!unresolved.linkExecutable(fresh, error) &&
          error.find("GetTickCount") != std::string::npos,
          "reject absent DLL export");
    check(read32(fresh.bytes, 0x2050) == 0x2090,
          "failed link leaves EXE IAT unchanged");

    // Reject a 64-bit DLL in a 32-bit Wine directory.
    auto wrongArch = emuwasm::test::makePe32Dll(
        "kernel32.dll", "GetTickCount");
    emuwasm::test::put16(wrongArch, 0x84, 0x8664); // IMAGE_FILE_MACHINE_AMD64
    save(root / "kernel32.dll", wrongArch);
    WineDllLoader incompatible;
    incompatible.addSearchDirectory(root);
    check(!incompatible.loadDll("kernel32.dll", error) &&
          error.find("i386") != std::string::npos,
          "reject non-i386 Wine DLL");

    fs::remove_all(root, ec);
    if (failures) {
        std::fprintf(stderr, "Wine DLL loader tests failed: %d\n", failures);
        return 1;
    }
    std::puts("Wine PE DLL disk loader, forwarded imports and rebasing passed");
    return 0;
}
