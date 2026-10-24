#include "emuwasm/pe32.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;
void expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b.at(at)) | (std::uint32_t(b.at(at + 1)) << 8) |
           (std::uint32_t(b.at(at + 2)) << 16) | (std::uint32_t(b.at(at + 3)) << 24);
}
} // namespace

int main() {
    using emuwasm::pe32::Image;
    using emuwasm::pe32::load;
    using emuwasm::test::put16;
    using emuwasm::test::put32;

    const std::vector<std::uint8_t> code = {0xb8, 0x34, 0x12, 0x40, 0x00, 0x90};
    const auto valid = emuwasm::test::makePe32(code);
    Image image;
    std::string error;
    expect(load(valid, 0x500000, image, error), "valid rebased PE32 loads");
    expect(image.preferredBase == 0x400000 && image.loadBase == 0x500000,
           "preferred and requested bases");
    expect(image.entryPoint == 0x501000, "entry point is ImageBase + entry RVA");
    expect(image.bytes.size() == 0x3000 && image.sections.size() == 2,
           "sections expanded into SizeOfImage");
    expect(get32(image.bytes, 0x1001) == 0x501234, "HIGHLOW relocation applied");
    expect(image.bytes[0x1800] == 0, "uninitialized virtual memory is zero");

    expect(load(valid, 0, image, error), "default preferred base loads");
    expect(get32(image.bytes, 0x1001) == 0x401234, "no rebase at preferred base");

    auto invalid = valid;
    invalid[0] = 0;
    expect(!load(invalid, 0, image, error), "reject invalid MZ signature");

    invalid = valid;
    put32(invalid, 0x3c, 0xfffffff0);
    expect(!load(invalid, 0, image, error), "reject truncated PE offset");

    invalid = valid;
    put16(invalid, 0x84, 0x8664);
    expect(!load(invalid, 0, image, error), "reject x64 machine");

    invalid = valid;
    put32(invalid, 0x178 + 40 + 12, 0x1000);
    expect(!load(invalid, 0, image, error), "reject overlapping sections");

    // The loader discovers imports, but only the resolver writes IAT pointers.
    const auto withImports = emuwasm::test::makePe32WithImports();
    expect(load(withImports, 0x500000, image, error),
           "load PE32 with IMAGE_IMPORT_DESCRIPTOR and HIGHLOW relocations");
    expect(image.imports.size() == 1, "one imported Windows symbol discovered");
    if (image.imports.size() == 1) {
        const auto& imported = image.imports[0];
        expect(imported.dll == "kernel32.dll" && imported.name == "GetTickCount" &&
               !imported.byOrdinal && imported.iatRva == 0x2050,
               "named import and IAT slot decoded");
    }
    expect(get32(image.bytes, 0x1002) == 0x502050,
           "absolute IAT memory operand rebased");
    expect(get32(image.bytes, 0x2050) == 0x2090,
           "unbound IAT retains import name RVA");

    int calls = 0;
    expect(bindImports(image,
           [&](const emuwasm::pe32::Import& imported, std::uint32_t& address) {
               ++calls;
               if (imported.dll != "kernel32.dll" ||
                   imported.name != "GetTickCount") return false;
               address = 0x501100; // a GUEST x86 stub, not a native function
               return true;
           }, error), "bind named import to guest address");
    expect(calls == 1 && get32(image.bytes, 0x2050) == 0x501100,
           "resolved guest pointer written to IAT");

    expect(load(withImports, 0x500000, image, error), "reload import fixture");
    expect(!bindImports(image,
           [](const emuwasm::pe32::Import&, std::uint32_t&) {
               return false;
           }, error) && error.find("kernel32.dll!GetTickCount") != std::string::npos,
           "missing DLL export produces actionable diagnostic");
    expect(get32(image.bytes, 0x2050) == 0x2090,
           "failed import binding does not modify IAT");

    invalid = withImports;
    put32(invalid, 0x440, 0x80000005u); // ordinal #5
    expect(load(invalid, 0, image, error), "load ordinal import");
    expect(image.imports.size() == 1 && image.imports[0].byOrdinal &&
           image.imports[0].ordinal == 5, "ordinal import decoded");
    expect(bindImports(image,
           [](const emuwasm::pe32::Import& imported, std::uint32_t& address) {
               if (!imported.byOrdinal || imported.ordinal != 5) return false;
               address = 0x401100;
               return true;
           }, error) && get32(image.bytes, 0x2050) == 0x401100,
           "resolve ordinal import");

    invalid = withImports;
    put32(invalid, 0x400, 0); // no OriginalFirstThunk; use FirstThunk
    expect(load(invalid, 0, image, error) && image.imports.size() == 1 &&
           image.imports[0].name == "GetTickCount",
           "FirstThunk fallback when OriginalFirstThunk is zero");

    invalid = withImports;
    put32(invalid, 0x440, 0x7fffffffu);
    expect(!load(invalid, 0, image, error),
           "reject IMAGE_IMPORT_BY_NAME RVA outside image");

    invalid = withImports;
    put32(invalid, 0x40c, 0x3fffu);
    expect(!load(invalid, 0, image, error), "reject unterminated DLL name");

    invalid = withImports;
    put32(invalid, 0x98 + 96 + 12, 20); // no null descriptor
    expect(!load(invalid, 0, image, error),
           "reject unterminated IMAGE_IMPORT_DESCRIPTOR array");

    invalid = withImports;
    put32(invalid, 0x98 + 96 + 8, 0x3fffu);
    expect(!load(invalid, 0, image, error),
           "reject import directory extending beyond image");

    // Two imports: failure on the second must not patch the first.
    invalid = withImports;
    put32(invalid, 0x444, 0x20b0); // second INT entry
    put32(invalid, 0x454, 0x20b0); // second IAT entry
    put16(invalid, 0x4b0, 0);
    constexpr char secondName[] = "MissingFunction";
    for (std::size_t i = 0; i < sizeof(secondName); ++i)
        invalid[0x4b2 + i] = static_cast<std::uint8_t>(secondName[i]);
    expect(load(invalid, 0, image, error) && image.imports.size() == 2,
           "parse multiple imports from one DLL");
    expect(!bindImports(image,
           [](const emuwasm::pe32::Import& imported, std::uint32_t& address) {
               if (imported.name == "MissingFunction") return false;
               address = 0x401100;
               return true;
           }, error), "reject second unresolved export");
    expect(get32(image.bytes, 0x2050) == 0x2090 &&
           get32(image.bytes, 0x2054) == 0x20b0,
           "failed multi-import binding is atomic");

    invalid = valid;
    put32(invalid, 0x98 + 96 + 5 * 8, 0);
    put32(invalid, 0x98 + 96 + 5 * 8 + 4, 0);
    expect(!load(invalid, 0x500000, image, error), "reject missing relocations on rebase");

    invalid = valid;
    put16(invalid, 0x408, 0x5001);
    expect(!load(invalid, 0x500000, image, error), "reject unsupported relocation type");

    invalid = valid;
    put32(invalid, 0x98 + 16, 0x2000);
    expect(!load(invalid, 0, image, error), "entry point must be executable");

    invalid = valid;
    put32(invalid, 0x98 + 56, 0xffffffff);
    expect(!load(invalid, 0, image, error), "reject unbounded image allocation");

    invalid = valid;
    invalid.resize(0x250);
    expect(!load(invalid, 0, image, error), "reject truncated section raw data");

    if (failures) {
        std::fprintf(stderr, "PE32 parser: %d failures\n", failures);
        return 1;
    }
    std::puts("PE32 parser: all validation and relocation tests passed");
    return 0;
}
