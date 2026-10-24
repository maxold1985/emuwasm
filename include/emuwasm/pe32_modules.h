#pragma once

#include "emuwasm/pe32.h"

#include <cstdint>
#include <string>
#include <vector>

namespace emuwasm {
namespace pe32 {

// Registry of PE32 DLL images in the SAME guest 32-bit address space as the
// EXE. This is a minimal Windows PE export resolver, NOT a Wine API runtime.
class ModuleRegistry {
public:
    // The DLL image must already have been parsed with load(..., true).
    // Each module occupies its own non-overlapping guest address range.
    bool addDll(std::string filename, Image image, std::string& error);

    // Resolve a named or ordinal import to an absolute GUEST x86 address.
    // Forwarders such as "ntdll.RtlFoo" and "ntdll.#12" are followed, with
    // cycle protection. This never returns a native host function pointer.
    bool resolve(const Import& imported, std::uint32_t& guestAddress) const;

    // Atomically patch a PE32 EXE or DLL's IAT using registered exports.
    bool linkImports(Image& image, std::string& error) const;

    const Image* findDll(const std::string& filename) const;

    // Link every registered DLL's own imports. All IAT writes are staged;
    // unresolved exports leave every DLL's IAT unchanged.
    bool linkAllImports(std::string& error);

    // Enumerate module names for mapping the DLL bytes into the guest MMU.
    std::vector<std::string> dllNames() const;

private:
    struct Module {
        std::string key;
        Image image;
    };
    std::vector<Module> modules_;

    bool resolveExport(const std::string& dll, const std::string& name,
                       std::uint32_t ordinal, bool byOrdinal,
                       std::uint32_t& address,
                       std::vector<std::string>& visited) const;
};

} // namespace pe32
} // namespace emuwasm
