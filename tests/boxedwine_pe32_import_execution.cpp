// PE32 import execution test: the original BoxedWine interpreter executes
// an indirect CALL through a Windows Import Address Table. The export target
// is a synthetic guest x86 stub, NOT a real Wine/kernel32 implementation.
#include "boxedwine.h"
#include "emuwasm/pe32.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>

int main() {
    constexpr U32 imageBase = 0x500000;
    constexpr U32 stackBase = 0x900000;
    constexpr U32 guestStub = imageBase + 0x1100;

    emuwasm::pe32::Image image;
    std::string error;
    if (!emuwasm::pe32::load(emuwasm::test::makePe32WithImports(),
                             imageBase, image, error)) {
        std::fprintf(stderr, "PE32 load failed: %s\n", error.c_str());
        return 1;
    }
    if (image.imports.size() != 1 ||
        !emuwasm::pe32::bindImports(
            image,
            [](const emuwasm::pe32::Import& imported, std::uint32_t& address) {
                if (imported.dll != "kernel32.dll" ||
                    imported.name != "GetTickCount" || imported.byOrdinal)
                    return false;
                address = guestStub; // guest VA, never a host function pointer
                return true;
            }, error)) {
        std::fprintf(stderr, "PE32 imports failed: %s\n", error.c_str());
        return 2;
    }

    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) return 3;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 4;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 5;
    KThread::setCurrentThread(thread);

    // Map expanded PE image including .text, .idata and .reloc.
    if (thread->memory->mmap(
            thread, imageBase, static_cast<U32>(image.bytes.size()),
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != imageBase)
        return 6;
    thread->memory->memcpy(imageBase, image.bytes.data(),
                           static_cast<U32>(image.bytes.size()));

    // A guest stack is essential for CALL/RET. Its addresses must belong to
    // BoxedWine's soft-MMU, not the native C++ process stack.
    if (thread->memory->mmap(
            thread, stackBase, K_PAGE_SIZE,
            K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != stackBase)
        return 7;
    if (thread->memory->mprotect(thread, imageBase, K_PAGE_SIZE, K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, imageBase + 0x1000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_EXEC) != 0 ||
        thread->memory->mprotect(thread, imageBase + 0x2000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_WRITE) != 0 ||
        thread->memory->mprotect(thread, imageBase + 0x3000, K_PAGE_SIZE,
                                 K_PROT_READ) != 0)
        return 8;

    thread->cpu->eip.u32 = image.entryPoint;
    thread->cpu->reg[0].u32 = 0;                  // EAX
    thread->cpu->reg[4].u32 = stackBase + K_PAGE_SIZE - 16; // ESP
    const U32 initialEsp = thread->cpu->reg[4].u32;

    // call [IAT], mov eax,10, ret, add eax,20, nop
    for (unsigned step = 0; step < 5; ++step) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "BoxedWine stopped at instruction %u\n", step);
            return 9;
        }
    }
    const U32 eax = thread->cpu->reg[0].u32;
    const U32 eip = thread->cpu->eip.u32;
    const U32 esp = thread->cpu->reg[4].u32;
    const U32 iat = thread->memory->readd(imageBase + 0x2050);
    std::printf("PE32 import CALL: EAX=%u EIP=%08x ESP=%08x IAT=%08x\n",
                eax, eip, esp, iat);
    return eax == 30 && eip == image.entryPoint + 10 &&
           esp == initialEsp && iat == guestStub ? 0 : 10;
}
