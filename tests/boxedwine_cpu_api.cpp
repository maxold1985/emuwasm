// Compile-time integration check against the ORIGINAL BoxedWine CPU API.
// This is not an instruction execution test. It deliberately avoids creating
// a CPU without the KMemory/KThread environment required by the interpreter.
#include "boxedwine.h"
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<decltype(CPU::allocCPU(std::declval<KMemory*>())), CPU*>,
              "BoxedWine CPU allocation API changed");
static_assert(std::is_same_v<decltype(std::declval<CPU&>().runNextSingleOp()), bool>,
              "BoxedWine single-instruction API changed");
static_assert(std::is_same_v<decltype(std::declval<CPU&>().reg[0].u32), U32>,
              "BoxedWine EAX register layout changed");

void emuwasm_boxedwine_cpu_api_check(CPU* cpu) {
    // Keep real methods referenced without executing on an uninitialized CPU.
    if (cpu) {
        cpu->reset();
    }
}
