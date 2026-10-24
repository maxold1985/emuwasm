// Load real Wine PE32 DLLs into an emulated image registry and resolve IATs.
// This is a diagnostic/linking tool: it does NOT execute Windows APIs.
// Example:
//   emuwasm_wine_link --wine-dir /usr/lib/i386-linux-gnu/wine/i386-windows game.exe
#include "emuwasm/pe32_wine.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    namespace fs = std::filesystem;
    std::vector<fs::path> directories;
    fs::path executable;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--wine-dir" && i + 1 < argc) {
            directories.emplace_back(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::printf("Usage: emuwasm_wine_link [--wine-dir PATH] "
                        "[--wine-dir PATH2] file.exe\n");
            return 0;
        } else if (!arg.empty() && arg[0] != '-' && executable.empty()) {
            executable = fs::path(arg);
        } else {
            std::fprintf(stderr, "Invalid argument: %s\n", arg.c_str());
            return 2;
        }
    }
    if (directories.empty()) {
        // Look for the Wine PE32 DLLs bundled alongside the WASM launcher.
        // A user-supplied --wine-dir always takes precedence.
        const fs::path bundled = fs::current_path() / "wine" / "i386-windows";
        if (fs::is_directory(bundled))
            directories.push_back(bundled);
    }
    if (executable.empty() || directories.empty()) {
        std::fprintf(stderr,
            "Usage: emuwasm_wine_link [--wine-dir PATH] file.exe\\n"
            "Run from the app folder containing wine/i386-windows, "
            "or specify --wine-dir.\\n");
        return 2;
    }

    emuwasm::pe32::WineDllLoader loader;
    for (const auto& directory : directories)
        loader.addSearchDirectory(directory);

    emuwasm::pe32::Image exe;
    std::string error;
    if (!loader.loadExecutable(executable, exe, error)) {
        std::fprintf(stderr, "PE32 EXE error: %s\n", error.c_str());
        return 3;
    }
    std::printf("EXE: %s at guest VA 0x%08x, %zu imports\n",
                executable.string().c_str(), exe.loadBase, exe.imports.size());

    if (!loader.linkExecutable(exe, error)) {
        std::fprintf(stderr, "Wine PE DLL link failed: %s\n", error.c_str());
        return 4;
    }

    for (const auto& name : loader.modules().dllNames()) {
        const auto* dll = loader.modules().findDll(name);
        if (!dll) continue;
        std::printf("Wine PE DLL: %-24s guest VA=0x%08x size=%zu "
                    "exports=%zu imports=%zu\n",
                    name.c_str(), dll->loadBase, dll->bytes.size(),
                    dll->exports.size(), dll->imports.size());
    }
    std::printf("Resolved %zu EXE imports using %zu PE DLLs.\n",
                exe.imports.size(), loader.modules().dllNames().size());
    std::puts("PE loading and IAT binding only. No Wine initialization, "
              "PEB/TEB, Unix calls or Windows API execution.");
    return 0;
}
