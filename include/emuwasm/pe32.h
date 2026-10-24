#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace emuwasm {
namespace pe32 {

// In-memory PE32 section layout. RVA values are relative to loadBase.
struct Section {
    std::string name;
    std::uint32_t rva = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
};

// One IMAGE_IMPORT_BY_NAME or ordinal import. iatRva identifies the guest
// pointer that the Windows PE loader would write to the Import Address Table.
struct Import {
    std::string dll;
    std::string name;                // empty for ordinal imports
    std::uint16_t hint = 0;
    std::uint16_t ordinal = 0;
    bool byOrdinal = false;
    std::uint32_t iatRva = 0;
};

// One export from an IMAGE_EXPORT_DIRECTORY. Forwarded exports contain a
// DLL.symbol (or DLL.#ordinal) string instead of executable code.
struct Export {
    std::string name;                 // empty for ordinal-only exports
    std::uint32_t ordinal = 0;       // IMAGE_EXPORT_DIRECTORY.Base + EAT index
    std::uint32_t rva = 0;
    std::string forwarder;            // nonempty if EAT RVA is inside export directory
};

struct Image {
    bool isDll = false;
    std::string exportModuleName;
    std::vector<Export> exports;
    std::uint32_t preferredBase = 0;
    std::uint32_t loadBase = 0;
    std::uint32_t entryPoint = 0; // absolute guest virtual address
    std::uint32_t sizeOfHeaders = 0;
    std::vector<std::uint8_t> bytes; // SizeOfImage, including zero-filled BSS
    std::vector<Section> sections;
    std::vector<Import> imports;     // imports are parsed but not automatically resolved
};

// Parse a little-endian x86 PE32 executable and apply HIGHLOW relocations.
// Imported DLL names, symbols, ordinals and IAT slots are discovered, but no
// host Windows code is invoked and no Wine DLLs are loaded by this function.
// Delay-load imports and TLS are not supported yet. Set allowDll=true for
// DLL images; ordinary EXEs must not be accepted as DLLs (or vice versa).
bool load(const std::vector<std::uint8_t>& file,
          std::uint32_t requestedBase,
          Image& output,
          std::string& error,
          bool allowDll = false);

// A future Wine-compatible module loader supplies GUEST virtual addresses,
// not native C++/WASM function pointers. A callback returning false denotes
// a missing DLL/export. Calls are transactional: no IAT writes on failure.
using ImportResolver = std::function<bool(const Import&, std::uint32_t& guestAddress)>;
bool bindImports(Image& image, const ImportResolver& resolver, std::string& error);

} // namespace pe32
} // namespace emuwasm
