#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace emuwasm {
namespace pe32 {

// PE32 image layout as consumed by a guest x86 CPU. The image bytes are
// already expanded from file offsets into RVAs and rebased when necessary.
// This module does NOT implement Windows DLL imports, TLS, or process startup.
struct Section {
    std::string name;
    std::uint32_t rva = 0;
    std::uint32_t virtualSize = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t characteristics = 0;
};

struct Image {
    std::uint32_t preferredBase = 0;
    std::uint32_t loadBase = 0;
    std::uint32_t entryPoint = 0; // absolute guest virtual address
    std::uint32_t sizeOfHeaders = 0;
    std::vector<std::uint8_t> bytes; // complete SizeOfImage, including BSS
    std::vector<Section> sections;
};

// Accepts only little-endian x86 PE32 executable images. Relocations of
// type IMAGE_REL_BASED_HIGHLOW are supported; imported DLLs are rejected
// explicitly until a Wine-compatible module resolver is connected.
// Returns false with a diagnostic for malformed or unsupported binaries.
bool load(const std::vector<std::uint8_t>& file,
          std::uint32_t requestedBase,
          Image& output,
          std::string& error);

} // namespace pe32
} // namespace emuwasm
