// End-to-end Windows PE32 EXE -> DLL export -> x86 CALL [IAT] regression.
// The DLL is a real PE32 *container* with IMAGE_EXPORT_DIRECTORY and guest
// x86 instructions, but is synthetic; it is NOT Wine's kernel32 implementation.
#include "boxedwine.h"
#include "emuwasm/pe32.h"
#include "emuwasm/pe32_modules.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>

int main() {
    constexpr U32 exeBase = 0x500000;
    constexpr U32 dllBase = 0x700000;
    constexpr U32 stackBase = 0x900000;
    constexpr U32 guestExport = dllBase + 0x1000;

    std::string error;
    emuwasm::pe32::Image exe;
    emuwasm::pe32::Image dll;
    if (!emuwasm::pe32::load(emuwasm::test::makePe32WithImports(),
                             exeBase, exe, error) ||
        !emuwasm::pe32::load(emuwasm::test::makePe32Dll(
            "kernel32.dll", "GetTickCount"), dllBase, dll, error, true)) {
        std::fprintf(stderr, "PE32 EXE/DLL loading failed: %s\n", error.c_str());
        return 1;
    }
    emuwasm::pe32::ModuleRegistry modules;
    if (!modules.addDll("KERNEL32.DLL", std::move(dll), error) ||
        !modules.linkImports(exe, error)) {
        std::fprintf(stderr, "PE32 DLL export resolution failed: %s\n", error.c_str());
        return 2;
    }
    const emuwasm::pe32::Image* dllImage = modules.findDll("kernel32.dll");
    if (!dllImage || exe.imports.size() != 1) return 3;

    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) return 4;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 5;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 6;
    KThread::setCurrentThread(thread);

    // Both PE images are mapped into the same BoxedWine soft-MMU guest space.
    // The EXE's CALL instruction fetches the DLL function VA from its IAT.
    if (thread->memory->mmap(
            thread, exeBase, static_cast<U32>(exe.bytes.size()),
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != exeBase)
        return 7;
    thread->memory->memcpy(exeBase, exe.bytes.data(),
                           static_cast<U32>(exe.bytes.size()));

    if (thread->memory->mmap(
            thread, dllBase, static_cast<U32>(dllImage->bytes.size()),
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != dllBase)
        return 8;
    thread->memory->memcpy(dllBase, dllImage->bytes.data(),
                           static_cast<U32>(dllImage->bytes.size()));

    if (thread->memory->mmap(
            thread, stackBase, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != stackBase)
        return 9;

    // Section protections: headers/.edata/.reloc read-only, .text RX, IAT RW.
    if (thread->memory->mprotect(thread, exeBase, K_PAGE_SIZE, K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, exeBase + 0x1000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_EXEC) != 0 ||
        thread->memory->mprotect(thread, exeBase + 0x2000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_WRITE) != 0 ||
        thread->memory->mprotect(thread, exeBase + 0x3000, K_PAGE_SIZE,
                                 K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, dllBase, K_PAGE_SIZE, K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, dllBase + 0x1000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_EXEC) != 0 ||
        thread->memory->mprotect(thread, dllBase + 0x2000, K_PAGE_SIZE,
                                 K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, dllBase + 0x3000, K_PAGE_SIZE,
                                 K_PROT_READ) != 0)
        return 10;

    thread->cpu->eip.u32 = exe.entryPoint;
    thread->cpu->reg[0].u32 = 0; // EAX
    thread->cpu->reg[4].u32 = stackBase + K_PAGE_SIZE - 16; // ESP
    const U32 initialEsp = thread->cpu->reg[4].u32;

    // 1: EXE call [IAT] -> DLL, 2: DLL mov eax,10, 3: DLL ret ->
    // 4: EXE add eax,20, 5: EXE nop.
    for (unsigned step = 0; step < 5; ++step) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "BoxedWine stopped at instruction %u\n", step);
            return 11;
        }
    }
    const U32 eax = thread->cpu->reg[0].u32;
    const U32 eip = thread->cpu->eip.u32;
    const U32 esp = thread->cpu->reg[4].u32;
    const U32 iat = thread->memory->readd(exeBase + 0x2050);
    std::printf("PE32 EXE->DLL export: EAX=%u EIP=%08x ESP=%08x IAT=%08x\n",
                eax, eip, esp, iat);
    return eax == 30 && eip == exe.entryPoint + 10 &&
           esp == initialEsp && iat == guestExport ? 0 : 12;
}
