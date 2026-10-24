// The ORIGINAL BoxedWine x86 interpreter reads Wine's PEB through FS:0x30.
// This proves the guest TEB is installed in an x86 TLS segment, not merely
// copied to memory. The environment is a minimal partial Windows skeleton.
#include "boxedwine.h"
#include "emuwasm/pe32_boxedwine.h"
#include "emuwasm/pe32_process.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>

int main() {
    using namespace emuwasm::pe32;
    constexpr U32 stackBase = 0x900000;
    constexpr U32 codeBase = 0x200000;
    constexpr U32 environmentBase = 0x12000000;

    std::string error;
    Image exe, kernel;
    if (!load(emuwasm::test::makePe32({0x90}), 0x500000, exe, error) ||
        !load(emuwasm::test::makePe32Dll(
                 "kernel32.dll", "GetTickCount"), 0x700000, kernel,
              error, true)) {
        std::fprintf(stderr, "PE fixture rejected: %s\n", error.c_str());
        return 1;
    }
    ModuleRegistry modules;
    if (!modules.addDll("kernel32.dll", std::move(kernel), error))
        return 2;

    Win32Environment env;
    if (!createWin32Environment(
            exe, modules, environmentBase, stackBase, K_PAGE_SIZE,
            31415, 2718, env, error)) {
        std::fprintf(stderr, "Win32 environment construction failed: %s\n",
                     error.c_str());
        return 3;
    }

    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) return 4;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 5;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 6;
    KThread::setCurrentThread(thread);

    if (!mapLinkedWineImages(thread, exe, modules, error) ||
        !installWin32Environment(thread, env, error)) {
        std::fprintf(stderr, "BoxedWine Win32 guest mapping failed: %s\n",
                     error.c_str());
        return 7;
    }
    if (thread->memory->mmap(
            thread, stackBase, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != stackBase)
        return 8;

    // 8 actual guest x86 instructions:
    //  1 mov eax, fs:[0x30]    ; Win32 TEB -> PEB
    //  2 mov ecx, [eax+0x08]   ; PEB.ImageBaseAddress
    //  3 mov edx, fs:[0x18]    ; NT_TIB.Self -> TEB
    //  4 mov ebx, [edx+0x20]   ; TEB.ClientId.UniqueProcess
    //  5 mov esi, [edx+0x24]   ; TEB.ClientId.UniqueThread
    //  6 mov ebp, [eax+0x0c]   ; PEB.Ldr
    //  7 mov ebp, [ebp+0x0c]   ; Ldr.InLoadOrderModuleList.Flink
    //  8 mov ebp, [ebp+0x18]   ; first LDR entry DllBase (EXE)
    constexpr U8 code[] = {
        0x64, 0xa1, 0x30, 0x00, 0x00, 0x00,
        0x8b, 0x48, 0x08,
        0x64, 0x8b, 0x15, 0x18, 0x00, 0x00, 0x00,
        0x8b, 0x5a, 0x20,
        0x8b, 0x72, 0x24,
        0x8b, 0x68, 0x0c,
        0x8b, 0x6d, 0x0c,
        0x8b, 0x6d, 0x18
    };
    if (thread->memory->mmap(
            thread, codeBase, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != codeBase)
        return 9;
    thread->memory->memcpy(codeBase, code, sizeof(code));
    thread->cpu->eip.u32 = codeBase;
    thread->cpu->reg[4].u32 = stackBase + K_PAGE_SIZE - 16;
    for (unsigned step = 0; step < 8; ++step) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "BoxedWine FS guest instruction %u failed\n", step);
            return 10;
        }
    }

    const U32 eax = thread->cpu->reg[0].u32;
    const U32 ecx = thread->cpu->reg[1].u32;
    const U32 edx = thread->cpu->reg[2].u32;
    const U32 ebx = thread->cpu->reg[3].u32;
    const U32 ebp = thread->cpu->reg[5].u32;
    const U32 esi = thread->cpu->reg[6].u32;
    const U32 eip = thread->cpu->eip.u32;
    std::printf("Win32 FS: PEB=%08x EXE=%08x TEB=%08x PID=%u TID=%u "
                "LDR_EXE=%08x EIP=%08x\n",
                eax, ecx, edx, ebx, esi, ebp, eip);

    return eax == env.peb && ecx == exe.loadBase &&
           edx == env.teb && ebx == env.processId &&
           esi == env.threadId && ebp == exe.loadBase &&
           eip == codeBase + sizeof(code) ? 0 : 11;
}
