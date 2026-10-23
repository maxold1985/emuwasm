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

    // The soft-MMU resolves guest RAM using the current host thread.
    // BoxedWine's normal process startup sets this immediately after
    // creating the guest KThread, before any guest memory access.
    KThread::setCurrentThread(thread);

    // 32-bit x86 program loaded as raw machine code in guest RAM.
    //
    //   mov eax, 10
    //   add eax, 20           ; eax = 30 (original smoke test)
    //   mov ecx, 3
    // loop:
    //   add eax, 2
    //   dec ecx
    //   jnz loop              ; exercise conditional branches and flags
    //   sub eax, 6           ; eax = 30 again
    //   mov ebx, eax         ; Linux i386 exit status
    //   mov eax, 1           ; Linux i386 sys_exit syscall number
    //   int 0x80             ; guest exit(30)
    constexpr U8 instructions[] = {
        0xB8, 0x0A, 0x00, 0x00, 0x00, // mov eax, 10
        0x83, 0xC0, 0x14,             // add eax, 20
        0xB9, 0x03, 0x00, 0x00, 0x00, // mov ecx, 3
        0x83, 0xC0, 0x02,             // loop: add eax, 2
        0x49,                         // dec ecx
        0x75, 0xFA,                   // jnz loop (relative -6)
        0x83, 0xE8, 0x06,             // sub eax, 6
        0x89, 0xC3,                   // mov ebx, eax
        0xB8, 0x01, 0x00, 0x00, 0x00, // mov eax, 1 (sys_exit)
        0xCD, 0x80                    // int 0x80
    };
    constexpr U32 address = 0x100000;
    constexpr U32 exitAddress = address + sizeof(instructions) - 2;
    U32 mapped = thread->memory->mmap(
        thread, address, K_PAGE_SIZE,
        K_PROT_READ | K_PROT_WRITE | K_PROT_EXEC,
        K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
        static_cast<FD>(-1), 0);
    if (mapped != address) {
        std::fprintf(stderr, "guest mmap failed: %08x\\n", mapped);
        return 3;
    }

    // This memory copy uses the original BoxedWine soft-MMU.
    thread->memory->memcpy(address, instructions, sizeof(instructions));
    thread->cpu->eip.u32 = address;
    thread->cpu->reg[0].u32 = 0;

    // Stop at the exit syscall first so register checks do not access a
    // CPU whose process might have been cleaned up by sys_exit.
    constexpr unsigned maxSteps = 64;
    unsigned steps = 0;
    while (thread->cpu->eip.u32 != exitAddress && steps < maxSteps) {
        if (!thread->cpu->runNextSingleOp()) {
            std::fprintf(stderr, "interpreter stopped at step %u\\n", steps);
            return 4;
        }
        ++steps;
    }
    if (thread->cpu->eip.u32 != exitAddress) {
        std::fprintf(stderr, "guest did not reach sys_exit after %u steps\\n", steps);
        return 5;
    }

    const U32 eax = thread->cpu->reg[0].u32;
    const U32 ebx = thread->cpu->reg[3].u32;
    const U32 ecx = thread->cpu->reg[1].u32;
    std::printf("Before exit: EAX=%u EBX=%u ECX=%u steps=%u\\n", eax, ebx, ecx, steps);
    std::fflush(stdout);
    if (eax != 1 || ebx != 30 || ecx != 0 || steps != 15) {
        std::fprintf(stderr, "incorrect loop or register state\\n");
        return 6;
    }

    // Execute the real BoxedWine Linux i386 syscall handler (sys_exit).
    // Do not access the KThread/CPU after exit: process teardown may free it.
    thread->cpu->run();
    std::printf("Guest exit: terminated=%d status=%u\\n",
                process->terminated ? 1 : 0, process->exitCode);
    return process->terminated && process->exitCode == 30 ? 0 : 8;
}
