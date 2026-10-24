#pragma once

#include "emuwasm/pe32_modules.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace emuwasm {
namespace pe32 {

// Loads genuine 32-bit PE DLL FILES from a user-supplied Wine i386-windows
// directory. The DLL code is NEVER executed by the native host process.
// Wine's PE DLLs also depend on its Unix-side runtime, PEB/TEB, etc.; this
// class resolves PE dependencies and IATs, not the full Windows ABI.
class WineDllLoader {
public:
    // Add paths such as /usr/lib/i386-linux-gnu/wine/i386-windows or an
    // extracted Wine i386-windows directory on Windows.
    void addSearchDirectory(const std::filesystem::path& directory);

    // Read and parse a PE32 EXE from disk, at its preferred base. The caller
    // owns the EXE image and can subsequently pass it to linkExecutable().
    bool loadExecutable(const std::filesystem::path& path,
                        Image& image, std::string& error);

    // Recursively load PE DLLs imported by this module and their forwarded
    // export targets. Case-insensitive filename lookup; no host LoadLibrary.
    bool loadDll(const std::string& filename, std::string& error);

    // Resolve the EXE and every DLL's IAT to GUEST x86 addresses. No x86 code
    // executes here. The caller must map these images into the BoxedWine MMU.
    bool linkExecutable(Image& exe, std::string& error);

    const ModuleRegistry& modules() const { return modules_; }

private:
    ModuleRegistry modules_;
    std::vector<std::filesystem::path> directories_;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> reserved_;
    std::uint64_t nextBase_ = 0x60000000ULL;

    bool loadDllRecursive(const std::string& filename,
                          std::string& error, unsigned depth);
    bool ensureForwarder(const Import& imported, std::string& error,
                         unsigned depth);
    bool findDllPath(const std::string& filename,
                     std::filesystem::path& path) const;
    bool available(std::uint64_t start, std::uint64_t length) const;
};

} // namespace pe32
} // namespace emuwasm
