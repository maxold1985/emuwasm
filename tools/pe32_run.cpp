// Headless Windows PE32 execution probe using the ORIGINAL BoxedWine CPU.
// Unlike emuwasm_wine_link, this advances real guest x86 instructions.
// It is NOT a Windows application runtime: imported Wine PE code requires
// Unix-call services, full ntdll startup and Windows APIs not present here.
#include "boxedwine.h"
#include "emuwasm/pe32_boxedwine.h"
#include "emuwasm/pe32_wine.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

struct Options {
    fs::path executable;
    std::vector<fs::path> directories;
    bool includeGameDirectory = true;
    bool trace = false;
    bool allowDllSteps = false;
    unsigned maxSteps = 10000;
};

void usage() {
    std::puts("Usage: emuwasm_pe32_run [--no-game-dir] [--game-dir PATH]"
              " [--wine-dir PATH] [--max-steps N] [--trace]"
              " [--allow-dll-steps] file.exe\n"
              "Native Linux/WSL BoxedWine CPU probe, not a game launcher.\n"
              "By default stops BEFORE executing a real Wine DLL entry point.");
}

bool parse(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            std::exit(0);
        } else if (arg == "--no-game-dir") {
            opts.includeGameDirectory = false;
        } else if (arg == "--trace") {
            opts.trace = true;
        } else if (arg == "--allow-dll-steps") {
            opts.allowDllSteps = true;
        } else if ((arg == "--wine-dir" || arg == "--game-dir") && i + 1 < argc) {
            opts.directories.emplace_back(argv[++i]);
        } else if (arg == "--max-steps" && i + 1 < argc) {
            const std::string value = argv[++i];
            try {
                std::size_t end = 0;
                const unsigned long long count = std::stoull(value, &end, 10);
                if (value.empty() || end != value.size() || count == 0 ||
                    count > 1000000) return false;
                opts.maxSteps = static_cast<unsigned>(count);
            } catch (...) {
                return false;
            }
        } else if (!arg.empty() && arg[0] != '-' && opts.executable.empty()) {
            opts.executable = fs::path(arg);
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n",
                         arg.c_str());
            return false;
        }
    }
    if (opts.executable.empty()) return false;
    if (opts.includeGameDirectory) {
        // Preserve the EXE directory ahead of Wine for private game DLLs.
        opts.directories.insert(opts.directories.begin(),
                                opts.executable.parent_path().empty()
                                    ? fs::current_path()
                                    : opts.executable.parent_path());
    }
    const fs::path bundled = fs::current_path() / "wine" / "i386-windows";
    if (fs::is_directory(bundled))
        opts.directories.push_back(bundled);
    return !opts.directories.empty();
}

bool inImage(U32 address, const emuwasm::pe32::Image& image) {
    return address >= image.loadBase &&
           static_cast<std::uint64_t>(address) - image.loadBase <
               image.bytes.size();
}

const std::string* owningDll(
    U32 address, const emuwasm::pe32::ModuleRegistry& modules,
    const std::vector<std::string>& names) {
    for (const auto& name : names) {
        const auto* dll = modules.findDll(name);
        if (dll && inImage(address, *dll)) return &name;
    }
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse(argc, argv, options)) {
        usage();
        return 2;
    }

    emuwasm::pe32::WineDllLoader wine;
    for (const auto& directory : options.directories)
        wine.addSearchDirectory(directory);

    emuwasm::pe32::Image exe;
    std::string error;
    if (!wine.loadExecutable(options.executable, exe, error) ||
        !wine.linkExecutable(exe, error)) {
        std::fprintf(stderr, "PE load/link failed: %s\n", error.c_str());
        return 3;
    }
    const auto names = wine.modules().dllNames();
    std::printf("Linked %zu EXE imports and %zu PE DLLs; entry EIP=%08x\n",
                exe.imports.size(), names.size(), exe.entryPoint);
    std::fflush(stdout);

    // Minimal guest Win32 process structures. Not Wine's real startup flow.
    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) {
        std::fputs("BoxedWine: process creation failed\n", stderr);
        return 4;
    }
    process->memory = KMemory::create(process.get());
    if (!process->memory) {
        std::fputs("BoxedWine: guest MMU creation failed\n", stderr);
        return 4;
    }
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) {
        std::fputs("BoxedWine: guest CPU/thread creation failed\n", stderr);
        return 4;
    }
    KThread::setCurrentThread(thread);

    // Ensure process metadata and stack do not overlap the EXE or DLLs.
    // The games may have different PE PreferredImageBases.
    constexpr U32 stackSize = 0x400000;  // 4 MiB.
    constexpr U32 envSize = 0x10000;
    U32 stackBase = 0;
    emuwasm::pe32::Win32Environment environment;
    for (std::uint64_t candidate = 0x20000000ULL;
         candidate <= 0x50000000ULL; candidate += 0x1000000ULL) {
        const U32 stack = static_cast<U32>(candidate);
        const U32 env = stack + 0x800000;
        if (emuwasm::pe32::createWin32Environment(
                exe, wine.modules(), env, stack, stackSize,
                process->id, thread->id, environment, error)) {
            stackBase = stack;
            break;
        }
    }
    if (!stackBase) {
        std::fprintf(stderr, "Cannot reserve guest Win32 stack/TEB: %s\n",
                     error.c_str());
        return 5;
    }

    if (!emuwasm::pe32::mapLinkedWineImages(
            thread, exe, wine.modules(), error)) {
        std::fprintf(stderr, "Guest PE mapping failed: %s\n", error.c_str());
        return 6;
    }
    if (thread->memory->mmap(
            thread, stackBase, stackSize, K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != stackBase ||
        !emuwasm::pe32::installWin32Environment(
            thread, environment, error)) {
        std::fprintf(stderr, "Guest stack/PEB/TEB setup failed: %s\n",
                     error.c_str());
        return 7;
    }

    thread->cpu->eip.u32 = exe.entryPoint;
    thread->cpu->reg[4].u32 = stackBase + stackSize - 16;  // ESP.
    thread->cpu->reg[0].u32 = 0;                            // EAX.

    std::printf("BoxedWine x86: stack=%08x PEB=%08x TEB=%08x FS=%08x\n",
                stackBase, environment.peb, environment.teb,
                thread->cpu->seg[FS].address);
    std::fflush(stdout);

    for (unsigned step = 0; step < options.maxSteps; ++step) {
        const U32 ip = thread->cpu->eip.u32;
        const std::string* dll = owningDll(ip, wine.modules(), names);

        if (dll && !options.allowDllSteps) {
            std::printf("STOP: reached %s at guest EIP=%08x after %u x86 "
                        "instructions. Wine DLL execution is NOT initialized.\n",
                        dll->c_str(), ip, step);
            return 0; // Expected diagnostic stop, not successful game launch.
        }
        if (!dll && !inImage(ip, exe)) {
            std::fprintf(stderr,
                         "STOP: EIP=%08x is outside the EXE and linked DLLs "
                         "after %u instructions\n", ip, step);
            return 8;
        }
        if (options.trace || (step != 0 && step % 1024 == 0)) {
            std::printf("x86 step=%u EIP=%08x EAX=%08x ESP=%08x%s\n",
                        step, ip, thread->cpu->reg[0].u32,
                        thread->cpu->reg[4].u32, dll ? " [Wine DLL]" : "");
            std::fflush(stdout);
        }
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr,
                         "STOP: BoxedWine CPU refused instruction at "
                         "EIP=%08x (step %u)\n", ip, step);
            return 9;
        }
    }
    std::printf("STOP: --max-steps=%u reached; final guest EIP=%08x. "
                "The game has NOT been launched.\n",
                options.maxSteps, thread->cpu->eip.u32);
    return 0; // Expected bounded diagnostic stop.
}
