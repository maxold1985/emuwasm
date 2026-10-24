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

    invalid = valid;
    put32(invalid, 0x98 + 96 + 8, 0x2000);
    put32(invalid, 0x98 + 96 + 12, 20);
    expect(!load(invalid, 0, image, error) &&
           error.find("imports") != std::string::npos,
           "reject imports until DLL resolver exists");

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
