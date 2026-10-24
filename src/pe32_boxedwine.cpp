#include "boxedwine.h"
#include "emuwasm/pe32_boxedwine.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace emuwasm {
namespace pe32 {

bool mapImageToBoxedWine(KThread* thread, const Image& image,
                         std::string& error) {
    error.clear();
    if (!thread || !thread->memory || image.bytes.empty() ||
        image.bytes.size() > 0xffffffffULL ||
        (image.loadBase & (K_PAGE_SIZE - 1)) != 0) {
        error = "invalid BoxedWine thread, PE image or guest image alignment";
        return false;
    }
    const U32 base = image.loadBase;
    const U32 size = static_cast<U32>(image.bytes.size());
    // Allocate the exact PE virtual address range in the guest soft-MMU.
    if (thread->memory->mmap(
            thread, base, size, K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != base) {
        error = "guest mmap failed for PE image at " + std::to_string(base);
        return false;
    }
    thread->memory->memcpy(base, image.bytes.data(), size);

    // IMAGE_SCN_MEM_READ/WRITE/EXECUTE control the guest page permissions.
    // Section RVA and image size are bounds checked by the PE32 parser.
    if (thread->memory->mprotect(thread, base, K_PAGE_SIZE, K_PROT_READ) != 0) {
        error = "failed to protect PE headers";
        return false;
    }
    for (const auto& section : image.sections) {
        const std::uint64_t length = std::max(section.virtualSize,
                                               section.rawSize);
        if (!length) continue;
        const std::uint64_t first = section.rva & ~(std::uint64_t(K_PAGE_SIZE) - 1);
        const std::uint64_t last = (std::uint64_t(section.rva) + length +
                                    K_PAGE_SIZE - 1) &
                                   ~(std::uint64_t(K_PAGE_SIZE) - 1);
        if (last > image.bytes.size() || last <= first) {
            error = "invalid PE section page range: " + section.name;
            return false;
        }
        U32 protection = 0;
        if (section.characteristics & 0x40000000u) protection |= K_PROT_READ;
        if (section.characteristics & 0x80000000u) protection |= K_PROT_WRITE;
        if (section.characteristics & 0x20000000u) protection |= K_PROT_EXEC;
        if (!protection) protection = K_PROT_READ;
        if (thread->memory->mprotect(
                thread, base + static_cast<U32>(first),
                static_cast<U32>(last - first), protection) != 0) {
            error = "failed to protect PE section: " + section.name;
            return false;
        }
    }
    return true;
}

bool mapLinkedWineImages(KThread* thread, const Image& exe,
                         const ModuleRegistry& modules, std::string& error) {
    if (!mapImageToBoxedWine(thread, exe, error)) return false;
    for (const auto& name : modules.dllNames()) {
        const Image* dll = modules.findDll(name);
        if (!dll) {
            error = "guest DLL disappeared from module registry: " + name;
            return false;
        }
        if (!mapImageToBoxedWine(thread, *dll, error)) {
            error = name + ": " + error;
            return false;
        }
    }
    return true;
}

bool installWin32Environment(KThread* thread,
                             const Win32Environment& environment,
                             std::string& error) {
    error.clear();
    if (!thread || !thread->cpu || !thread->memory ||
        environment.bytes.empty() ||
        environment.bytes.size() > 0xffffffffULL ||
        (environment.base & (K_PAGE_SIZE - 1)) != 0 ||
        environment.teb != environment.base + 0x1000) {
        error = "invalid Win32 environment or BoxedWine guest thread";
        return false;
    }

    const U32 base = environment.base;
    const U32 size = static_cast<U32>(environment.bytes.size());
    if (thread->memory->mmap(
            thread, base, size, K_PROT_READ | K_PROT_WRITE,
            K_MAP_PRIVATE | K_MAP_ANONYMOUS | K_MAP_FIXED,
            static_cast<FD>(-1), 0) != base) {
        error = "failed to map Win32 PEB/TEB/LDR in BoxedWine guest memory";
        return false;
    }
    thread->memory->memcpy(base, environment.bytes.data(), size);

    // BoxedWine's CPU::setSegment reads user_desc via KThread::getLDT.
    // Select one thread-local GDT slot reserved by its Linux TLS emulator.
    // The selector's low bits request the thread-local descriptor.
    user_desc descriptor = {};
    descriptor.entry_number = TLS_ENTRY_START_INDEX;
    descriptor.base_addr = environment.teb;
    descriptor.limit = K_PAGE_SIZE - 1;
    descriptor.seg_32bit = 1;
    descriptor.contents = 0;
    descriptor.read_exec_only = 0;
    descriptor.limit_in_pages = 0;
    descriptor.seg_not_present = 0;
    descriptor.useable = 1;
    thread->setTLS(&descriptor);

    constexpr U32 selector = (TLS_ENTRY_START_INDEX << 3) | 7;
    if (!thread->cpu->setSegment(FS, selector) ||
        thread->cpu->seg[FS].address != environment.teb) {
        error = "failed to initialize Windows x86 FS segment in BoxedWine";
        return false;
    }
    return true;
}

} // namespace pe32
} // namespace emuwasm
