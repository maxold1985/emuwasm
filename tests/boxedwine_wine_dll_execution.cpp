// BoxedWine CPU executes EXE -> kernel32.dll -> ntdll.dll with PE32
// files loaded FROM DISK by WineDllLoader. All functions are synthetic
// x86 guest code; no actual Windows API is implemented by this test.
#include "boxedwine.h"
#include "emuwasm/pe32_wine.h"
#include "emuwasm/pe32_boxedwine.h"
#include "pe32_fixture.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
void save(const std::filesystem::path& path,
          const std::vector<std::uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("emuwasm-wine-x86-" + std::to_string(nonce));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return 1;

    save(root / "app.exe", emuwasm::test::makePe32WithImports());
    save(root / "kernel32.dll", emuwasm::test::makePe32DllWithImports());
    save(root / "ntdll.dll", emuwasm::test::makePe32Dll(
        "ntdll.dll", "RtlGetTickCount"));

    emuwasm::pe32::WineDllLoader wine;
    wine.addSearchDirectory(root);
    emuwasm::pe32::Image exe;
    std::string error;
    if (!wine.loadExecutable(root / "app.exe", exe, error) ||
        !wine.linkExecutable(exe, error)) {
        std::fprintf(stderr, "Wine-style PE DLL linking failed: %s\n",
                     error.c_str());
        return 2;
    }
    const auto* kernel = wine.modules().findDll("kernel32.dll");
    const auto* ntdll = wine.modules().findDll("ntdll.dll");
    if (!kernel || !ntdll) return 3;

    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) return 4;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 5;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 6;
    KThread::setCurrentThread(thread);

    if (!emuwasm::pe32::mapLinkedWineImages(
            thread, exe, wine.modules(), error)) {
        std::fprintf(stderr, "Failed to map PE images into BoxedWine guest MMU: %s\\n",
                     error.c_str());
        return 7;
    }

    constexpr U32 stackBase = 0x900000;
    if (thread->memory->mmap(
            thread, stackBase, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != stackBase)
        return 8;

    // The same process that runs Wine-style PE DLL code also has a guest
    // Windows TEB/PEB and FS segment, not only executable memory mappings.
    emuwasm::pe32::Win32Environment win32;
    if (!emuwasm::pe32::createWin32Environment(
            exe, wine.modules(), 0x12000000, stackBase, K_PAGE_SIZE,
            process->id, thread->id, win32, error) ||
        !emuwasm::pe32::installWin32Environment(thread, win32, error)) {
        std::fprintf(stderr, "Win32 process startup skeleton failed: %s\\n",
                     error.c_str());
        return 13;
    }

    thread->cpu->eip.u32 = exe.entryPoint;
    thread->cpu->reg[0].u32 = 0;
    thread->cpu->reg[4].u32 = stackBase + K_PAGE_SIZE - 16;
    const U32 initialEsp = thread->cpu->reg[4].u32;

    // EXE CALL [IAT], kernel32 CALL [IAT], ntdll MOV EAX,10,
    // ntdll RET, kernel32 RET, EXE ADD EAX,20, EXE NOP.
    for (unsigned step = 0; step < 7; ++step) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "BoxedWine CPU stopped at step %u\n", step);
            return 9;
        }
    }
    const U32 eax = thread->cpu->reg[0].u32;
    const U32 eip = thread->cpu->eip.u32;
    const U32 esp = thread->cpu->reg[4].u32;
    const U32 exeIat = thread->memory->readd(exe.loadBase + 0x2050);
    const U32 kernelIat = thread->memory->readd(kernel->loadBase + 0x3050);
    std::printf("Wine DLL guest call chain: EAX=%u EIP=%08x ESP=%08x "
                "EXE_IAT=%08x DLL_IAT=%08x\n",
                eax, eip, esp, exeIat, kernelIat);

    fs::remove_all(root, ec);
    return eax == 30 && eip == exe.entryPoint + 10 &&
           esp == initialEsp &&
           exeIat == kernel->loadBase + 0x1000 &&
           kernelIat == ntdll->loadBase + 0x1000 &&
           thread->cpu->seg[FS].address == win32.teb &&
           thread->memory->readd(win32.teb + 0x30) == win32.peb &&
           thread->memory->readd(win32.peb + 0x08) == exe.loadBase ? 0 : 10;
}
