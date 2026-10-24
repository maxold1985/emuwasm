#pragma once

#include "emuwasm/pe32_modules.h"
#include "emuwasm/pe32_process.h"

#include <string>

class KThread;

namespace emuwasm {
namespace pe32 {

// Map an already parsed/rebased PE32 image into the ORIGINAL BoxedWine
// guest soft-MMU. Uses guest virtual addresses and PE section R/W/X flags.
// This does NOT initialize Windows PEB/TEB, DLL entry points or Wine Unix calls.
bool mapImageToBoxedWine(KThread* thread, const Image& image,
                         std::string& error);

// Map an EXE and every linked DLL into one BoxedWine process address space.
// Call only after WineDllLoader::linkExecutable() has resolved IATs.
bool mapLinkedWineImages(KThread* thread, const Image& exe,
                         const ModuleRegistry& modules, std::string& error);

// Map the guest PEB/TEB/LDR memory and install the Windows x86 TEB in
// FS using BoxedWine's ORIGINAL KThread TLS/LDT mechanism.
bool installWin32Environment(KThread* thread,
                             const Win32Environment& environment,
                             std::string& error);

} // namespace pe32
} // namespace emuwasm
