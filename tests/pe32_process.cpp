#include "emuwasm/pe32_process.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::uint32_t at(const emuwasm::pe32::Win32Environment& e, std::uint32_t address) {
    if (address < e.base || address + 4ull > e.base + e.bytes.size()) return 0;
    const std::uint32_t off = address - e.base;
    return static_cast<std::uint32_t>(e.bytes[off]) |
           (static_cast<std::uint32_t>(e.bytes[off + 1]) << 8) |
           (static_cast<std::uint32_t>(e.bytes[off + 2]) << 16) |
           (static_cast<std::uint32_t>(e.bytes[off + 3]) << 24);
}
} // namespace

int main() {
    using namespace emuwasm::pe32;
    Image exe, kernel, ntdll;
    std::string error;
    check(load(emuwasm::test::makePe32WithImports(), 0x500000, exe, error),
          "load EXE fixture");
    check(load(emuwasm::test::makePe32Dll("kernel32.dll", "GetTickCount"),
               0x700000, kernel, error, true), "load kernel32 DLL fixture");
    check(load(emuwasm::test::makePe32Dll("ntdll.dll", "RtlGetTickCount"),
               0x800000, ntdll, error, true), "load ntdll DLL fixture");
    ModuleRegistry modules;
    check(modules.addDll("kernel32.dll", kernel, error) &&
          modules.addDll("ntdll.dll", ntdll, error),
          "register both DLLs");

    constexpr std::uint32_t envBase = 0x12000000;
    constexpr std::uint32_t stackBase = 0x900000;
    constexpr std::uint32_t stackSize = 0x2000;
    Win32Environment env;
    check(createWin32Environment(exe, modules, envBase, stackBase,
                                 stackSize, 1234, 77, env, error),
          "construct Win32 guest process structures");
    check(env.base == envBase && env.bytes.size() == 0x10000 &&
          env.peb == envBase && env.teb == envBase + 0x1000 &&
          env.ldr == envBase + 0x2000, "PEB/TEB/LDR guest addresses");

    check(at(env, env.peb + 0x08) == exe.loadBase &&
          at(env, env.peb + 0x0c) == env.ldr &&
          at(env, env.peb + 0x10) == env.processParameters,
          "Wine x86 PEB image, loader and parameters fields");
    check(at(env, env.teb + 0x00) == 0xffffffffu &&
          at(env, env.teb + 0x04) == stackBase + stackSize &&
          at(env, env.teb + 0x08) == stackBase &&
          at(env, env.teb + 0x18) == env.teb &&
          at(env, env.teb + 0x20) == 1234 &&
          at(env, env.teb + 0x24) == 77 &&
          at(env, env.teb + 0x30) == env.peb,
          "Wine x86 TEB stack, exception chain, self, IDs and Peb");

    const std::uint32_t firstEntry = envBase + 0x3000;
    const std::uint32_t lastEntry = envBase + 0x3200;
    for (unsigned order = 0; order < 3; ++order) {
        const std::uint32_t off = order * 8;
        const std::uint32_t head = env.ldr + 0x0c + off;
        check(at(env, head) == firstEntry + off &&
              at(env, head + 4) == lastEntry + off,
              "loader list head points to first and last module");
        const std::uint32_t first = at(env, head);
        const std::uint32_t second = at(env, first);
        const std::uint32_t third = at(env, second);
        check(first == firstEntry + off &&
              second == firstEntry + 0x100 + off &&
              third == lastEntry + off &&
              at(env, third) == head,
              "loader list closes in a circular chain");
        check(at(env, first + 4) == head &&
              at(env, third + 4) == second,
              "loader list backward chain is consistent");
    }
    check(at(env, firstEntry + 0x18) == exe.loadBase &&
          at(env, firstEntry + 0x100 + 0x18) == kernel.loadBase &&
          at(env, firstEntry + 0x200 + 0x18) == ntdll.loadBase,
          "LDR_DATA_TABLE_ENTRY DllBase fields");
    check(at(env, env.processParameters + 0x08) == 1 &&
          at(env, env.processParameters + 0x38 + 4) >= envBase + 0x8000,
          "normalized RTL_USER_PROCESS_PARAMETERS and UTF-16 paths");

    check(!createWin32Environment(exe, modules, exe.loadBase, stackBase,
                                  stackSize, 1234, 77, env, error),
          "reject PEB/TEB overlapping EXE");
    check(!createWin32Environment(exe, modules, envBase, kernel.loadBase,
                                  stackSize, 1234, 77, env, error),
          "reject stack overlapping guest DLL");
    check(!createWin32Environment(exe, modules, envBase, stackBase,
                                  stackSize, 0, 77, env, error),
          "reject invalid Windows process ID");

    if (failures) {
        std::fprintf(stderr, "Win32 guest environment tests: %d failures\n",
                     failures);
        return 1;
    }
    std::puts("Win32 x86 TEB/PEB/LDR/ProcessParameters tests passed");
    return 0;
}
