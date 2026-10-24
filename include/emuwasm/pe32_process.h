#pragma once

#include "emuwasm/pe32_modules.h"

#include <cstdint>
#include <string>
#include <vector>

namespace emuwasm {
namespace pe32 {

// Minimal x86 Windows process memory skeleton, using verified Wine 32-bit
// winternl.h field offsets. It is intentionally NOT Wine ntdll startup:
// loader synchronization, exception dispatch, process heap, API sets,
// activation contexts and actual Windows syscalls remain unimplemented.
struct Win32Environment {
    std::uint32_t base = 0;
    std::uint32_t peb = 0;
    std::uint32_t teb = 0;
    std::uint32_t ldr = 0;
    std::uint32_t processParameters = 0;
    std::uint32_t processId = 0;
    std::uint32_t threadId = 0;
    std::vector<std::uint8_t> bytes;
};

// Constructs PEB, TEB, RTL_USER_PROCESS_PARAMETERS and doubly linked
// LDR_DATA_TABLE_ENTRY lists in GUEST linear memory. Input images must have
// non-overlapping guest VAs. stackBottom + stackSize is the guest stack top.
// No native host addresses are stored in the guest structures.
bool createWin32Environment(const Image& exe,
                            const ModuleRegistry& modules,
                            std::uint32_t environmentBase,
                            std::uint32_t stackBottom,
                            std::uint32_t stackSize,
                            std::uint32_t processId,
                            std::uint32_t threadId,
                            Win32Environment& output,
                            std::string& error);

} // namespace pe32
} // namespace emuwasm
