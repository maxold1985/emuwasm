#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

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

// PE32 import fixture: a CALL DWORD PTR [0x402050] instruction at the entry
// point, a guest stub at RVA 0x1100, and one kernel32.dll!GetTickCount import.
// The stub is deliberately a test double, NOT an implementation of kernel32.
// .idata holds the import descriptor, OriginalFirstThunk and IAT.
inline std::vector<std::uint8_t> makePe32WithImports() {
    const std::vector<std::uint8_t> code = {
        0xff, 0x15, 0x50, 0x20, 0x40, 0x00, // call dword ptr [0x402050]
        0x83, 0xc0, 0x14,                   // add eax,20
        0x90                                // nop
    };
    auto b = makePe32(code);
    b.resize(0x800, 0);

    put16(b, 0x86, 3);                // .text, .idata, .reloc
    put32(b, 0x98 + 56, 0x4000);      // SizeOfImage
    put32(b, 0x98 + 96 + 8, 0x2000);  // import descriptor RVA
    put32(b, 0x98 + 96 + 12, 40);     // descriptor + terminator
    put32(b, 0x98 + 96 + 5 * 8, 0x3000);
    put32(b, 0x98 + 96 + 5 * 8 + 4, 12);

    constexpr std::size_t idata = 0x178 + 40;
    b[idata] = '.'; b[idata+1] = 'i'; b[idata+2] = 'd';
    b[idata+3] = 'a'; b[idata+4] = 't'; b[idata+5] = 'a';
    put32(b, idata + 36, 0xc0000040); // initialized, read/write

    constexpr std::size_t reloc = idata + 40;
    b[reloc] = '.'; b[reloc+1] = 'r'; b[reloc+2] = 'e';
    b[reloc+3] = 'l'; b[reloc+4] = 'o'; b[reloc+5] = 'c';
    put32(b, reloc + 8, 0x200);
    put32(b, reloc + 12, 0x3000);
    put32(b, reloc + 16, 0x200);
    put32(b, reloc + 20, 0x600);
    put32(b, reloc + 36, 0x42000040);

    // IMAGE_IMPORT_DESCRIPTOR at file 0x400 == RVA 0x2000.
    put32(b, 0x400, 0x2040);          // OriginalFirstThunk (INT)
    put32(b, 0x40c, 0x2080);          // DLL name
    put32(b, 0x410, 0x2050);          // FirstThunk (IAT)
    put32(b, 0x440, 0x2090);          // INT: IMAGE_IMPORT_BY_NAME RVA
    put32(b, 0x450, 0x2090);          // IAT: initially same RVA

    constexpr char dll[] = "kernel32.dll";
    constexpr char symbol[] = "GetTickCount";
    for (std::size_t i = 0; i < sizeof(dll); ++i)
        b[0x480 + i] = static_cast<std::uint8_t>(dll[i]);
    put16(b, 0x490, 0);               // IMAGE_IMPORT_BY_NAME.Hint
    for (std::size_t i = 0; i < sizeof(symbol); ++i)
        b[0x492 + i] = static_cast<std::uint8_t>(symbol[i]);

    // Guest export test stub at RVA 0x1100: mov eax,10; ret
    b[0x300] = 0xb8; b[0x301] = 10;
    b[0x302] = 0; b[0x303] = 0; b[0x304] = 0;
    b[0x305] = 0xc3;

    // Fix up absolute IAT operand at .text+2 on rebase.
    put32(b, 0x600, 0x1000);
    put32(b, 0x604, 12);
    put16(b, 0x608, 0x3002);
    put16(b, 0x60a, 0);
    return b;
}

// Synthetic PE32 DLL with two exports: a named function at ordinal 1,
// and an ordinal-only function at ordinal 2. Optionally forward ordinal 1
// to another DLL. The x86 code is guest code (mov eax,imm32; ret).
inline std::vector<std::uint8_t> makePe32Dll(
        const std::string& moduleName,
        const std::string& exportName,
        const std::string& forwarder = {}) {
    if (moduleName.empty() || moduleName.size() >= 16 ||
        exportName.empty() || exportName.size() >= 16 ||
        forwarder.size() >= 48)
        return {};

    auto b = makePe32({0xb8, 10, 0, 0, 0, 0xc3});
    b.resize(0x800, 0);
    put16(b, 0x86, 3);               // .text, .edata, .reloc
    put16(b, 0x96, 0x2102);           // IMAGE_FILE_DLL + executable + i386
    put32(b, 0x98 + 16, 0);          // DLL without DllMain
    put32(b, 0x98 + 28, 0x600000);   // preferred DLL base
    put32(b, 0x98 + 56, 0x4000);     // 4 virtual pages
    put32(b, 0x98 + 96, 0x2000);     // export directory RVA
    put32(b, 0x98 + 96 + 4, 0xc0);   // export directory size
    put32(b, 0x98 + 96 + 5 * 8, 0x3000);
    put32(b, 0x98 + 96 + 5 * 8 + 4, 12);

    // Change the old .reloc section into .edata.
    constexpr std::size_t edata = 0x178 + 40;
    for (unsigned i = 0; i < 8; ++i) b[edata + i] = 0;
    b[edata] = '.'; b[edata+1] = 'e'; b[edata+2] = 'd';
    b[edata+3] = 'a'; b[edata+4] = 't'; b[edata+5] = 'a';
    put32(b, edata + 36, 0x40000040); // initialized read-only data

    constexpr std::size_t reloc = edata + 40;
    b[reloc] = '.'; b[reloc+1] = 'r'; b[reloc+2] = 'e';
    b[reloc+3] = 'l'; b[reloc+4] = 'o'; b[reloc+5] = 'c';
    put32(b, reloc + 8, 0x200);
    put32(b, reloc + 12, 0x3000);
    put32(b, reloc + 16, 0x200);
    put32(b, reloc + 20, 0x600);
    put32(b, reloc + 36, 0x42000040);

    // IMAGE_EXPORT_DIRECTORY at file 0x400 / RVA 0x2000.
    put32(b, 0x40c, 0x2070);         // Name RVA
    put32(b, 0x410, 1);              // Base ordinal
    put32(b, 0x414, 2);              // NumberOfFunctions
    put32(b, 0x418, 1);              // NumberOfNames
    put32(b, 0x41c, 0x2040);         // AddressOfFunctions
    put32(b, 0x420, 0x2050);         // AddressOfNames
    put32(b, 0x424, 0x2060);         // AddressOfNameOrdinals
    put32(b, 0x440, forwarder.empty() ? 0x1000 : 0x2090);
    put32(b, 0x444, 0x1010);         // ordinal 2 guest code
    put32(b, 0x450, 0x2080);         // GetTickCount or RtlGetTickCount
    put16(b, 0x460, 0);              // name maps to EAT[0]
    for (std::size_t i = 0; i <= moduleName.size(); ++i)
        b[0x470 + i] = i == moduleName.size() ? 0
            : static_cast<std::uint8_t>(moduleName[i]);
    for (std::size_t i = 0; i <= exportName.size(); ++i)
        b[0x480 + i] = i == exportName.size() ? 0
            : static_cast<std::uint8_t>(exportName[i]);
    for (std::size_t i = 0; i <= forwarder.size(); ++i)
        b[0x490 + i] = i == forwarder.size() ? 0
            : static_cast<std::uint8_t>(forwarder[i]);

    // Ordinal 2 returns 11; named ordinal 1 returns 10.
    b[0x210] = 0xb8; b[0x211] = 11;
    b[0x212] = 0; b[0x213] = 0; b[0x214] = 0;
    b[0x215] = 0xc3;
    put32(b, 0x600, 0x1000);         // relocation block, ABSOLUTE only
    put32(b, 0x604, 12);
    put16(b, 0x608, 0);
    put16(b, 0x60a, 0);
    return b;
}

} // namespace test
} // namespace emuwasm
