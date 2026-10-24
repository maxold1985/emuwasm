// Inspect a PE32 EXE or DLL from disk without executing host/guest code.
// Usage: emuwasm_pe32_inspect [--dll] [--base 0x700000] path/to/module.dll
#include "emuwasm/pe32.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
void usage() {
    std::fprintf(stderr,
        "Usage: emuwasm_pe32_inspect [--dll] [--base 0xHEX] file.exe|file.dll\n");
}
bool parseBase(const char* text, std::uint32_t& value) {
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long long number = std::strtoull(text, &end, 0);
    if (errno || !end || *end || number > 0xffffffffULL) return false;
    value = static_cast<std::uint32_t>(number);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    bool dll = false;
    std::uint32_t base = 0;
    std::string filename;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--dll") {
            dll = true;
        } else if (arg == "--base" && i + 1 < argc) {
            if (!parseBase(argv[++i], base)) {
                usage();
                return 2;
            }
        } else if (!arg.empty() && arg[0] != '-' && filename.empty()) {
            filename = arg;
        } else {
            usage();
            return 2;
        }
    }
    if (filename.empty()) {
        usage();
        return 2;
    }

    std::ifstream stream(filename, std::ios::binary | std::ios::ate);
    if (!stream) {
        std::fprintf(stderr, "Cannot open: %s\n", filename.c_str());
        return 3;
    }
    const auto length = stream.tellg();
    if (length <= 0 || length > static_cast<std::streamoff>(256 * 1024 * 1024)) {
        std::fprintf(stderr, "PE32 input is empty or exceeds 256 MiB\n");
        return 3;
    }
    std::vector<std::uint8_t> file(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(file.data()),
                     static_cast<std::streamsize>(file.size()))) {
        std::fprintf(stderr, "Failed to read PE32 file\n");
        return 3;
    }

    emuwasm::pe32::Image image;
    std::string error;
    if (!emuwasm::pe32::load(file, base, image, error, dll)) {
        std::fprintf(stderr, "PE32 validation failed: %s\n", error.c_str());
        return 4;
    }
    std::printf("Type: %s\nPreferred base: 0x%08x\nGuest load base: 0x%08x\n"
                "Entry point: 0x%08x\nImage size: %zu bytes\n",
                image.isDll ? "DLL" : "EXE", image.preferredBase,
                image.loadBase, image.entryPoint, image.bytes.size());
    for (const auto& section : image.sections)
        std::printf("Section %-8s RVA=0x%08x VirtualSize=0x%x Flags=0x%08x\n",
                    section.name.c_str(), section.rva, section.virtualSize,
                    section.characteristics);

    std::printf("Exports (%zu) from %s:\n", image.exports.size(),
                image.exportModuleName.empty() ? "(none)"
                                               : image.exportModuleName.c_str());
    for (const auto& exported : image.exports) {
        std::printf("  #%u %-32s RVA=0x%08x",
                    exported.ordinal,
                    exported.name.empty() ? "(ordinal only)" : exported.name.c_str(),
                    exported.rva);
        if (!exported.forwarder.empty())
            std::printf(" -> %s", exported.forwarder.c_str());
        std::putchar('\n');
    }
    std::printf("Imports (%zu; not bound):\n", image.imports.size());
    for (const auto& imported : image.imports) {
        if (imported.byOrdinal)
            std::printf("  %s!#%u IAT=0x%08x\n", imported.dll.c_str(),
                        imported.ordinal, imported.iatRva);
        else
            std::printf("  %s!%s IAT=0x%08x\n", imported.dll.c_str(),
                        imported.name.c_str(), imported.iatRva);
    }
    return 0;
}
