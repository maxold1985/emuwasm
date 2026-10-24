#include "emuwasm/pe32_process.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace emuwasm {
namespace pe32 {
namespace {

constexpr std::uint32_t kAreaSize = 0x10000;
constexpr std::uint32_t kTebOffset = 0x1000;
constexpr std::uint32_t kLdrOffset = 0x2000;
constexpr std::uint32_t kParametersOffset = 0x2800;
constexpr std::uint32_t kEntriesOffset = 0x3000;
constexpr std::uint32_t kEntryStride = 0x100;
constexpr std::uint32_t kNamesOffset = 0x8000;
constexpr std::uint32_t kMaxModules = (kNamesOffset - kEntriesOffset) / kEntryStride;

bool overlaps(std::uint64_t a, std::uint64_t aLen,
              std::uint64_t b, std::uint64_t bLen) {
    return a < b + bLen && b < a + aLen;
}

void w16(std::vector<std::uint8_t>& bytes,
         std::uint32_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void w32(std::vector<std::uint8_t>& bytes,
         std::uint32_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

// Write a UTF-16LE UNICODE_STRING in the guest image. Windows length values
// are BYTE counts excluding the NUL terminator, not UTF-16 code-unit counts.
bool unicode(std::vector<std::uint8_t>& bytes, std::uint32_t base,
             std::uint32_t descriptorOffset,
             std::uint32_t& cursor, const std::string& utf8Ascii) {
    if (utf8Ascii.size() > 250 ||
        cursor + (utf8Ascii.size() + 1) * 2 > bytes.size())
        return false;
    for (unsigned char c : utf8Ascii)
        if (c < 0x20 || c > 0x7e) return false;

    const auto length = static_cast<std::uint16_t>(utf8Ascii.size() * 2);
    w16(bytes, descriptorOffset, length);
    w16(bytes, descriptorOffset + 2, length + 2);
    w32(bytes, descriptorOffset + 4, base + cursor);
    for (const unsigned char ch : utf8Ascii) {
        w16(bytes, cursor, ch);
        cursor += 2;
    }
    w16(bytes, cursor, 0);
    cursor += 2;
    return true;
}

struct Module {
    std::string file;
    const Image* image = nullptr;
};

} // namespace

bool createWin32Environment(const Image& exe,
                            const ModuleRegistry& modules,
                            std::uint32_t environmentBase,
                            std::uint32_t stackBottom,
                            std::uint32_t stackSize,
                            std::uint32_t processId,
                            std::uint32_t threadId,
                            Win32Environment& output,
                            std::string& error) {
    error.clear();
    output = Win32Environment{};
    const std::uint64_t envEnd = std::uint64_t(environmentBase) + kAreaSize;
    const std::uint64_t stackEnd = std::uint64_t(stackBottom) + stackSize;
    if (exe.isDll || exe.bytes.empty() || !environmentBase ||
        (environmentBase & 0xfff) || envEnd > 0x100000000ULL ||
        !stackBottom || !stackSize || stackEnd > 0x100000000ULL ||
        stackEnd <= stackBottom ||
        overlaps(environmentBase, kAreaSize, stackBottom, stackSize) ||
        processId == 0 || threadId == 0) {
        error = "invalid Windows EXE, environment range or guest stack";
        return false;
    }

    std::vector<Module> list{{"app.exe", &exe}};
    for (const auto& name : modules.dllNames()) {
        const Image* dll = modules.findDll(name);
        if (!dll || !dll->isDll) {
            error = "missing or invalid module while constructing Win32 LDR";
            return false;
        }
        list.push_back({name, dll});
    }
    if (list.size() > kMaxModules) {
        error = "too many DLLs for minimal Win32 loader list";
        return false;
    }
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& image = *list[i].image;
        const std::uint64_t length = image.bytes.size();
        if (!length || std::uint64_t(image.loadBase) + length > 0x100000000ULL ||
            overlaps(environmentBase, kAreaSize, image.loadBase, length) ||
            overlaps(stackBottom, stackSize, image.loadBase, length)) {
            error = "guest DLL or EXE overlaps PEB/TEB or stack";
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const auto& prev = *list[j].image;
            if (overlaps(image.loadBase, length, prev.loadBase, prev.bytes.size())) {
                error = "overlapping PE modules in guest address space";
                return false;
            }
        }
    }

    Win32Environment env;
    env.base = environmentBase;
    env.peb = environmentBase;
    env.teb = environmentBase + kTebOffset;
    env.ldr = environmentBase + kLdrOffset;
    env.processParameters = environmentBase + kParametersOffset;
    env.processId = processId;
    env.threadId = threadId;
    env.bytes.resize(kAreaSize, 0);
    auto& bytes = env.bytes;

    // PEB x86 offsets from Wine winternl.h:
    // +0x08 ImageBaseAddress, +0x0c LdrData, +0x10 ProcessParameters.
    w32(bytes, 0x08, exe.loadBase);
    w32(bytes, 0x0c, env.ldr);
    w32(bytes, 0x10, env.processParameters);

    // TEB / NT_TIB x86 offsets:
    // +0x00 ExceptionList (-1 = no SEH handlers), +0x04 StackBase,
    // +0x08 StackLimit, +0x18 Self, +0x20/0x24 CLIENT_ID,
    // +0x30 Peb, +0x34 LastErrorValue.
    w32(bytes, kTebOffset + 0x00, 0xffffffffu);
    w32(bytes, kTebOffset + 0x04, static_cast<std::uint32_t>(stackEnd));
    w32(bytes, kTebOffset + 0x08, stackBottom);
    w32(bytes, kTebOffset + 0x18, env.teb);
    w32(bytes, kTebOffset + 0x20, processId);
    w32(bytes, kTebOffset + 0x24, threadId);
    w32(bytes, kTebOffset + 0x30, env.peb);

    // The loader lists use a LIST_ENTRY head, followed by one
    // LDR_DATA_TABLE_ENTRY per executable/DLL. Link each list's
    // Flink/Blink to the corresponding entry's embedded LIST_ENTRY.
    w32(bytes, kLdrOffset + 0x00, 0x30); // PEB_LDR_DATA.Length
    bytes[kLdrOffset + 0x04] = 1;        // Initialized
    for (unsigned order = 0; order < 3; ++order) {
        const std::uint32_t listOffset = 0x0c + order * 8;
        const std::uint32_t head = env.ldr + listOffset;
        const std::uint32_t entryLinkOffset = order * 8;
        const std::uint32_t first = environmentBase + kEntriesOffset + entryLinkOffset;
        const std::uint32_t last = environmentBase + kEntriesOffset +
                                   static_cast<std::uint32_t>(list.size() - 1) *
                                       kEntryStride + entryLinkOffset;
        w32(bytes, kLdrOffset + listOffset + 0, first);
        w32(bytes, kLdrOffset + listOffset + 4, last);
        for (std::size_t i = 0; i < list.size(); ++i) {
            const std::uint32_t at = kEntriesOffset +
                                      static_cast<std::uint32_t>(i) * kEntryStride +
                                      entryLinkOffset;
            const std::uint32_t previous = i == 0 ? head :
                environmentBase + kEntriesOffset +
                static_cast<std::uint32_t>(i - 1) * kEntryStride + entryLinkOffset;
            const std::uint32_t next = i + 1 == list.size() ? head :
                environmentBase + kEntriesOffset +
                static_cast<std::uint32_t>(i + 1) * kEntryStride + entryLinkOffset;
            w32(bytes, at, next);
            w32(bytes, at + 4, previous);
        }
    }

    std::uint32_t cursor = kNamesOffset;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::uint32_t at = kEntriesOffset +
                                  static_cast<std::uint32_t>(i) * kEntryStride;
        const auto& module = list[i];
        w32(bytes, at + 0x18, module.image->loadBase);
        w32(bytes, at + 0x1c, module.image->entryPoint);
        w32(bytes, at + 0x20,
            static_cast<std::uint32_t>(module.image->bytes.size()));

        const std::string fullName = i == 0 ?
            "C:\\emuwasm\\app.exe" :
            "C:\\windows\\system32\\" + module.file;
        if (!unicode(bytes, environmentBase, at + 0x24, cursor, fullName) ||
            !unicode(bytes, environmentBase, at + 0x2c, cursor, module.file)) {
            error = "Win32 module filename cannot fit UTF-16 guest memory";
            return false;
        }
    }

    // A minimal RTL_USER_PROCESS_PARAMETERS (NOT the complete Wine
    // process-parameter initialization): Length, normalized flag,
    // ImagePathName at +0x38 and CommandLine at +0x40.
    w32(bytes, kParametersOffset + 0x00, 0x300);
    w32(bytes, kParametersOffset + 0x04, 0x300);
    w32(bytes, kParametersOffset + 0x08, 1);
    if (!unicode(bytes, environmentBase, kParametersOffset + 0x38,
                 cursor, "C:\\emuwasm\\app.exe") ||
        !unicode(bytes, environmentBase, kParametersOffset + 0x40,
                 cursor, "\"C:\\emuwasm\\app.exe\"")) {
        error = "Win32 process parameter string allocation failed";
        return false;
    }

    output = std::move(env);
    return true;
}

} // namespace pe32
} // namespace emuwasm
