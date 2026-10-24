// Execute instructions from a synthetic Windows PE32 image using the
// ORIGINAL BoxedWine CPU and soft-MMU. This does not emulate Win32 APIs yet.
#include "boxedwine.h"
#include "emuwasm/pe32.h"
#include "pe32_fixture.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

int main() {
    // MOV EBX,0x401234 (rebased by the PE loader), MOV EAX,10,
    // ADD EAX,20, NOP. No Windows imports or syscalls are needed.
    const std::vector<std::uint8_t> code = {
        0xbb, 0x34, 0x12, 0x40, 0x00,
        0xb8, 0x0a, 0x00, 0x00, 0x00,
        0x83, 0xc0, 0x14,
        0x90
    };
    constexpr U32 guestBase = 0x500000;
    emuwasm::pe32::Image image;
    std::string error;
    if (!emuwasm::pe32::load(emuwasm::test::makePe32(code),
                             guestBase, image, error)) {
        std::fprintf(stderr, "PE32 loader rejected fixture: %s\n", error.c_str());
        return 1;
    }

    KSystem::init(true);
    KProcessPtr process = KProcess::create();
    if (!process) return 2;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 3;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 4;
    KThread::setCurrentThread(thread);

    // Reserve the entire PE virtual image, including zero-filled BSS.
    const U32 mapped = thread->memory->mmap(
        thread, guestBase, static_cast<U32>(image.bytes.size()),
        K_PROT_READ | K_PROT_WRITE,
        K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
        static_cast<FD>(-1), 0);
    if (mapped != guestBase) {
        std::fprintf(stderr, "PE32 guest mmap failed: %08x\n", mapped);
        return 5;
    }
    thread->memory->memcpy(guestBase, image.bytes.data(),
                           static_cast<U32>(image.bytes.size()));

    // Match the fixture's section protections after the initial copy.
    // Headers/.reloc are read-only; .text is read+execute, never writable.
    if (thread->memory->mprotect(thread, guestBase, K_PAGE_SIZE, K_PROT_READ) != 0 ||
        thread->memory->mprotect(thread, guestBase + 0x1000, K_PAGE_SIZE,
                                 K_PROT_READ | K_PROT_EXEC) != 0 ||
        thread->memory->mprotect(thread, guestBase + 0x2000, K_PAGE_SIZE,
                                 K_PROT_READ) != 0)
        return 6;

    thread->cpu->eip.u32 = image.entryPoint;
    thread->cpu->reg[0].u32 = 0;
    thread->cpu->reg[3].u32 = 0;
    for (unsigned step = 0; step < 4; ++step) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "PE32 CPU stopped at step %u\n", step);
            return 7;
        }
    }

    const U32 eax = thread->cpu->reg[0].u32;
    const U32 ebx = thread->cpu->reg[3].u32;
    const U32 eip = thread->cpu->eip.u32;
    std::printf("PE32 guest: entry=%08x EAX=%u EBX=%08x EIP=%08x\n",
                image.entryPoint, eax, ebx, eip);
    return eax == 30 && ebx == 0x501234 &&
           eip == image.entryPoint + code.size() ? 0 : 8;
}
