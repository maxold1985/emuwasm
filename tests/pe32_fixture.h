#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emuwasm {
namespace test {

// Small deterministic PE32 console executable with .text and .reloc.
// Constructed in memory: no Windows toolchain, Wine binary, or external
// copyrighted executable is needed for the regression tests.
inline void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b.at(at) = static_cast<std::uint8_t>(v);
    b.at(at + 1) = static_cast<std::uint8_t>(v >> 8);
}
inline void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = static_cast<std::uint8_t>(v >> (8 * i));
}

inline std::vector<std::uint8_t> makePe32(const std::vector<std::uint8_t>& code) {
    std::vector<std::uint8_t> b(0x600, 0);
    if (code.size() > 0x200) return {};

    put16(b, 0, 0x5a4d);                 // DOS MZ
    put32(b, 0x3c, 0x80);                // e_lfanew
    put32(b, 0x80, 0x00004550);         // PE signature
    put16(b, 0x84, 0x014c);             // IMAGE_FILE_MACHINE_I386
    put16(b, 0x86, 2);                  // section count
    put16(b, 0x94, 0x00e0);             // SizeOfOptionalHeader
    put16(b, 0x96, 0x0102);             // executable, 32-bit

    constexpr std::size_t opt = 0x98;
    put16(b, opt, 0x010b);              // PE32 (not PE32+)
    put32(b, opt + 16, 0x1000);         // AddressOfEntryPoint
    put32(b, opt + 20, 0x1000);         // BaseOfCode
    put32(b, opt + 24, 0x2000);         // BaseOfData
    put32(b, opt + 28, 0x400000);       // ImageBase
    put32(b, opt + 32, 0x1000);         // SectionAlignment
    put32(b, opt + 36, 0x200);          // FileAlignment
    put32(b, opt + 56, 0x3000);         // SizeOfImage
    put32(b, opt + 60, 0x200);          // SizeOfHeaders
    put16(b, opt + 68, 3);             // IMAGE_SUBSYSTEM_WINDOWS_CUI
    put32(b, opt + 92, 16);            // NumberOfRvaAndSizes
    put32(b, opt + 96 + 5 * 8, 0x2000);// Base reloc directory RVA
    put32(b, opt + 96 + 5 * 8 + 4, 12);// Base reloc directory size

    constexpr std::size_t text = 0x178;
    b[text] = '.'; b[text+1] = 't'; b[text+2] = 'e';
    b[text+3] = 'x'; b[text+4] = 't';
    put32(b, text + 8, 0x200);          // VirtualSize
    put32(b, text + 12, 0x1000);        // VirtualAddress
    put32(b, text + 16, 0x200);         // SizeOfRawData
    put32(b, text + 20, 0x200);         // PointerToRawData
    put32(b, text + 36, 0x60000020);    // executable, readable code

    constexpr std::size_t reloc = text + 40;
    b[reloc] = '.'; b[reloc+1] = 'r'; b[reloc+2] = 'e';
    b[reloc+3] = 'l'; b[reloc+4] = 'o'; b[reloc+5] = 'c';
    put32(b, reloc + 8, 0x200);
    put32(b, reloc + 12, 0x2000);
    put32(b, reloc + 16, 0x200);
    put32(b, reloc + 20, 0x400);
    put32(b, reloc + 36, 0x42000040);   // initialized, readable data

    for (std::size_t i = 0; i < code.size(); ++i)
        b[0x200 + i] = code[i];

    // Relocate a 32-bit absolute address at .text+1 (e.g. MOV EAX,imm32).
    put32(b, 0x400, 0x1000);            // relocation page RVA
    put32(b, 0x404, 12);                // block size
    put16(b, 0x408, 0x3001);            // HIGHLOW, offset 1
    put16(b, 0x40a, 0);                 // ABSOLUTE padding
    return b;
}

} // namespace test
} // namespace emuwasm
