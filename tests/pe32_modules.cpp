#include "emuwasm/pe32.h"
#include "emuwasm/pe32_modules.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* description) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
std::uint32_t read32(const std::vector<std::uint8_t>& bytes, std::size_t rva) {
    return std::uint32_t(bytes.at(rva)) |
           (std::uint32_t(bytes.at(rva + 1)) << 8) |
           (std::uint32_t(bytes.at(rva + 2)) << 16) |
           (std::uint32_t(bytes.at(rva + 3)) << 24);
}
} // namespace

int main() {
    using namespace emuwasm::pe32;
    std::string error;
    Image kernel, ntdll, main;
    const auto kernelFile = emuwasm::test::makePe32Dll(
        "kernel32.dll", "GetTickCount");
    check(!load(kernelFile, 0x700000, kernel, error),
          "EXE parser must reject DLL flag");
    check(load(kernelFile, 0x700000, kernel, error, true),
          "load synthetic kernel32 PE32 DLL at nonpreferred base");
    check(kernel.isDll && kernel.entryPoint == 0x700000 &&
          kernel.exports.size() == 2 && kernel.exportModuleName == "kernel32.dll",
          "parse DLL with no DllMain and two exports");
    if (kernel.exports.size() == 2) {
        check(kernel.exports[0].name == "GetTickCount" &&
              kernel.exports[0].ordinal == 1 && kernel.exports[0].rva == 0x1000,
              "named EAT export parsed");
        check(kernel.exports[1].name.empty() &&
              kernel.exports[1].ordinal == 2 && kernel.exports[1].rva == 0x1010,
              "ordinal-only EAT export parsed");
    }

    ModuleRegistry modules;
    check(modules.addDll("KERNEL32.DLL", kernel, error),
          "register case-insensitive DLL filename");
    check(modules.findDll("kernel32") != nullptr,
          "find DLL with omitted extension");
    check(!modules.addDll("kernel32", kernel, error),
          "reject duplicate module name");
    check(!modules.addDll("other.dll", kernel, error),
          "reject overlapping guest image ranges");

    Import request;
    request.dll = "Kernel32.Dll";
    request.name = "GetTickCount";
    std::uint32_t address = 0;
    check(modules.resolve(request, address) && address == 0x701000,
          "resolve named export to DLL guest address");
    request.byOrdinal = true;
    request.ordinal = 2;
    request.name.clear();
    check(modules.resolve(request, address) && address == 0x701010,
          "resolve ordinal-only export");
    request.ordinal = 99;
    check(!modules.resolve(request, address), "reject missing export ordinal");

    check(load(emuwasm::test::makePe32WithImports(), 0x500000, main, error),
          "load EXE with kernel32 IAT");
    check(modules.linkImports(main, error) &&
          read32(main.bytes, 0x2050) == 0x701000,
          "patch EXE IAT to DLL guest code");

    const auto forwarded = emuwasm::test::makePe32Dll(
        "kernel32.dll", "GetTickCount", "ntdll.RtlGetTickCount");
    check(load(forwarded, 0x700000, kernel, error, true) &&
          kernel.exports.size() == 2 &&
          kernel.exports[0].forwarder == "ntdll.RtlGetTickCount",
          "parse named forwarded export");
    check(load(emuwasm::test::makePe32Dll(
          "ntdll.dll", "RtlGetTickCount"), 0x800000, ntdll, error, true),
          "load second DLL");
    ModuleRegistry chain;
    check(chain.addDll("kernel32.dll", kernel, error) &&
          chain.addDll("ntdll.dll", ntdll, error),
          "register two DLLs in separate guest memory ranges");
    request.byOrdinal = false;
    request.name = "GetTickCount";
    request.ordinal = 0;
    check(chain.resolve(request, address) && address == 0x801000,
          "follow kernel32.GetTickCount -> ntdll.RtlGetTickCount");
    check(load(emuwasm::test::makePe32WithImports(), 0x500000, main, error) &&
          chain.linkImports(main, error) &&
          read32(main.bytes, 0x2050) == 0x801000,
          "link EXE through a forwarded DLL export");

    // Forwarding by ordinal: ntdll.#2 resolves to the second x86 stub.
    auto ordinalForward = emuwasm::test::makePe32Dll(
        "kernel32.dll", "GetTickCount", "ntdll.#2");
    check(load(ordinalForward, 0x700000, kernel, error, true),
          "load ordinal-forwarding DLL");
    ModuleRegistry ordinalChain;
    check(ordinalChain.addDll("kernel32", kernel, error) &&
          ordinalChain.addDll("ntdll", ntdll, error) &&
          ordinalChain.resolve(request, address) && address == 0x801010,
          "follow forwarded export by ordinal");

    // A -> B -> A must stop, not recurse forever.
    auto circular = emuwasm::test::makePe32Dll(
        "ntdll.dll", "RtlGetTickCount", "kernel32.GetTickCount");
    check(load(circular, 0x800000, ntdll, error, true),
          "load circular forwarder DLL");
    ModuleRegistry cycle;
    check(cycle.addDll("kernel32", kernel, error) &&
          cycle.addDll("ntdll", ntdll, error), "register circular modules");
    // kernel currently forwards to ntdll.#2 (non-cyclic). Switch it.
    check(load(forwarded, 0x700000, kernel, error, true),
          "restore named forwarder");
    ModuleRegistry actualCycle;
    check(actualCycle.addDll("kernel32", kernel, error) &&
          actualCycle.addDll("ntdll", ntdll, error) &&
          !actualCycle.resolve(request, address),
          "reject circular DLL export forwarders");

    // PE permits multiple names to alias the same EAT index.
    auto aliases = kernelFile;
    emuwasm::test::put32(aliases, 0x418, 2); // NumberOfNames
    emuwasm::test::put32(aliases, 0x454, 0x20a0); // second name RVA
    emuwasm::test::put16(aliases, 0x462, 0);      // same EAT index 0
    constexpr char aliasName[] = "GetTickCountAlias";
    for (std::size_t i = 0; i < sizeof(aliasName); ++i)
        aliases[0x4a0 + i] = static_cast<std::uint8_t>(aliasName[i]);
    check(load(aliases, 0x700000, kernel, error, true) &&
          kernel.exports.size() == 3,
          "retain two export names for one EAT slot");
    ModuleRegistry aliased;
    check(aliased.addDll("kernel32.dll", kernel, error),
          "register DLL with aliased export");
    request.name = "GetTickCountAlias";
    check(aliased.resolve(request, address) && address == 0x701000,
          "resolve second export alias to same guest address");
    request.name = "GetTickCount";

    auto malformed = kernelFile;
    emuwasm::test::put32(malformed, 0x41c, 0xfffffff0);
    check(!load(malformed, 0x700000, kernel, error, true),
          "reject EAT pointer outside DLL image");

    malformed = kernelFile;
    emuwasm::test::put16(malformed, 0x460, 9);
    check(!load(malformed, 0x700000, kernel, error, true),
          "reject invalid named export ordinal index");

    malformed = kernelFile;
    emuwasm::test::put32(malformed, 0x440, 0x2090);
    malformed[0x490] = 'x'; malformed[0x491] = 0;
    check(!load(malformed, 0x700000, kernel, error, true),
          "reject malformed export forwarder");

    if (failures) {
        std::fprintf(stderr, "PE32 module tests failed: %d\n", failures);
        return 1;
    }
    std::puts("PE32 DLL export, ordinal, forwarder and IAT tests passed");
    return 0;
}
