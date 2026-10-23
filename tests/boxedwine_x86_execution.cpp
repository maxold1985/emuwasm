// Integration test using the ORIGINAL BoxedWine interpreter and memory.
// Requires the full BoxedWine runtime to be linked (not just CPU/kernel objects).
#include "boxedwine.h"
#include <cstdint>
#include <cstdio>

int main() {
    // Initialize BoxedWine's scheduler, guest page size and soft-MMU backing
    // before constructing the first process. Keep linear 4 GiB reservation off
    // in this small headless execution test.
    KSystem::init(true);
    // KProcess::create publishes the process but does not allocate its memory.
    // BoxedWine normally calls KMemory::create before createThread in startProcess.
    KProcessPtr process = KProcess::create();
    if (!process) return 1;
    process->memory = KMemory::create(process.get());
    if (!process->memory) return 7;
    KThread* thread = process->createThread();
    if (!thread || !thread->cpu || !thread->memory) return 2;

    // mov eax, 10 ; add eax, 20 (32-bit x86)
    constexpr U8 instructions[] = {
        0xB8, 0x0A, 0x00, 0x00, 0x00,
        0x83, 0xC0, 0x14
    };
    constexpr U32 address = 0x100000;
    U32 mapped = thread->memory->mmap(
        thread, address, K_PAGE_SIZE,
        K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC,
        K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
        static_cast<FD>(-1), 0);
    if (mapped != address) {
        std::fprintf(stderr, "guest mmap failed: %08x\\n", mapped);
        return 3;
    }
    thread->memory->memcpy(address, instructions, sizeof(instructions));
    thread->cpu->eip.u32 = address;
    thread->cpu->reg[0].u32 = 0;

    // Each call must execute the upstream decoder/normal interpreter.
    if (!thread->cpu->runNextSingleOp()) return 4;
    if (!thread->cpu->runNextSingleOp()) return 5;
    const U32 eax = thread->cpu->reg[0].u32;
    const U32 eip = thread->cpu->eip.u32;
    std::printf("EAX=%u EIP=%08x\\n", eax, eip);
    return eax == 30 && eip == address + sizeof(instructions) ? 0 : 6;
}
