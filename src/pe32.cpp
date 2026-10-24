#include "emuwasm/pe32.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace emuwasm {
namespace pe32 {
namespace {

constexpr std::uint16_t kMachineI386 = 0x014c;
constexpr std::uint16_t kPe32Magic = 0x010b;
constexpr std::uint32_t kMaxImageSize = 256u * 1024u * 1024u;
constexpr std::uint32_t kExecutableSection = 0x20000000u;

bool fits(std::size_t size, std::uint64_t start, std::uint64_t count) {
    return start <= size && count <= size - start;
}

bool read16(const std::vector<std::uint8_t>& data, std::uint64_t at,
            std::uint16_t& value) {
    if (!fits(data.size(), at, 2)) return false;
    value = static_cast<std::uint16_t>(data[at])
          | (static_cast<std::uint16_t>(data[at + 1]) << 8);
    return true;
}

bool read32(const std::vector<std::uint8_t>& data, std::uint64_t at,
            std::uint32_t& value) {
    if (!fits(data.size(), at, 4)) return false;
    value = static_cast<std::uint32_t>(data[at])
          | (static_cast<std::uint32_t>(data[at + 1]) << 8)
          | (static_cast<std::uint32_t>(data[at + 2]) << 16)
          | (static_cast<std::uint32_t>(data[at + 3]) << 24);
    return true;
}

void write32(std::vector<std::uint8_t>& data, std::uint32_t at,
             std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        data[at + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

bool directory(const std::vector<std::uint8_t>& file,
               std::uint64_t optional, std::uint16_t optionalSize,
               std::uint32_t count, std::uint32_t index,
               std::uint32_t& rva, std::uint32_t& size) {
    rva = size = 0;
    if (count <= index) return true;
    const std::uint64_t at = optional + 96u + index * 8u;
    if (at + 8 > optional + optionalSize) return false;
    return read32(file, at, rva) && read32(file, at + 4, size);
}

bool fail(std::string& error, const char* reason) {
    error = reason;
    return false;
}

bool asciiZ(const std::vector<std::uint8_t>& bytes, std::uint64_t rva,
            std::size_t maxLength, std::string& result) {
    result.clear();
    if (rva >= bytes.size()) return false;
    for (std::size_t i = 0; i < maxLength; ++i) {
        if (!fits(bytes.size(), rva + i, 1)) return false;
        const auto ch = bytes[static_cast<std::size_t>(rva + i)];
        if (!ch) return !result.empty();
        if (ch < 0x20 || ch > 0x7e) return false;
        result.push_back(static_cast<char>(ch));
    }
    return false; // missing NUL within the allowed name length
}

// Parse IMAGE_IMPORT_DESCRIPTOR and thunk arrays from the EXPANDED image,
// not from file offsets. Wine's PE loader follows this same RVA distinction.
// A separate module loader must later supply guest export addresses.
bool parseImports(Image& image, std::uint32_t importRva,
                  std::uint32_t importSize, std::string& error) {
    if (!importRva && !importSize) return true;
    if (!importRva || !importSize ||
        !fits(image.bytes.size(), importRva, importSize))
        return fail(error, "invalid import directory range");

    const std::uint64_t end = static_cast<std::uint64_t>(importRva) + importSize;
    std::uint64_t at = importRva;
    bool terminated = false;
    for (unsigned descriptor = 0; descriptor < 256; ++descriptor) {
        if (end - at < 20)
            return fail(error, "unterminated import descriptor table");

        std::uint32_t fields[5] = {};
        for (unsigned n = 0; n < 5; ++n)
            if (!read32(image.bytes, at + n * 4, fields[n]))
                return fail(error, "truncated import descriptor");
        at += 20;

        if (!fields[0] && !fields[1] && !fields[2] &&
            !fields[3] && !fields[4]) {
            terminated = true;
            break;
        }
        const std::uint32_t lookupRva = fields[0] ? fields[0] : fields[4];
        const std::uint32_t dllNameRva = fields[3];
        const std::uint32_t iatRva = fields[4];
        std::string dll;
        if (!lookupRva || !iatRva ||
            !asciiZ(image.bytes, dllNameRva, 260, dll))
            return fail(error, "invalid import DLL name or thunk RVA");

        bool thunkTerminated = false;
        for (unsigned n = 0; n < 8192; ++n) {
            const std::uint64_t lookupAt = static_cast<std::uint64_t>(lookupRva) + n * 4u;
            const std::uint64_t iatAt = static_cast<std::uint64_t>(iatRva) + n * 4u;
            std::uint32_t thunk = 0, iatValue = 0;
            if (!read32(image.bytes, lookupAt, thunk) ||
                !read32(image.bytes, iatAt, iatValue))
                return fail(error, "import thunk/IAT exceeds image bounds");
            if (!thunk) {
                thunkTerminated = true;
                break;
            }
            if (image.imports.size() >= 8192)
                return fail(error, "too many PE imports");
            Import imported;
            imported.dll = dll;
            imported.iatRva = static_cast<std::uint32_t>(iatAt);
            imported.byOrdinal = (thunk & 0x80000000u) != 0;
            if (imported.byOrdinal) {
                if (thunk & 0x7fff0000u)
                    return fail(error, "invalid ordinal import thunk");
                imported.ordinal = static_cast<std::uint16_t>(thunk);
            } else {
                const std::uint32_t nameRva = thunk;
                if (!read16(image.bytes, nameRva, imported.hint) ||
                    !asciiZ(image.bytes, static_cast<std::uint64_t>(nameRva) + 2,
                            1024, imported.name))
                    return fail(error, "invalid IMAGE_IMPORT_BY_NAME entry");
            }
            image.imports.push_back(std::move(imported));
        }
        if (!thunkTerminated)
            return fail(error, "unterminated import thunk array");
    }
    if (!terminated)
        return fail(error, "too many import descriptors or missing terminator");
    return true;
}

// IMAGE_EXPORT_DIRECTORY is a table of RVAs. Forwarded exports point to
// ASCII "module.symbol" strings inside the export directory's RVA range.
bool parseExports(Image& image, std::uint32_t exportRva,
                  std::uint32_t exportSize, std::string& error) {
    if (!exportRva && !exportSize) return true;
    if (!exportRva || exportSize < 40 ||
        !fits(image.bytes.size(), exportRva, exportSize))
        return fail(error, "invalid PE export directory range");

    const auto& bytes = image.bytes;
    std::uint32_t dllName = 0, ordinalBase = 0, functionCount = 0;
    std::uint32_t nameCount = 0, functionTable = 0, nameTable = 0, ordinalTable = 0;
    if (!read32(bytes, exportRva + 12, dllName) ||
        !read32(bytes, exportRva + 16, ordinalBase) ||
        !read32(bytes, exportRva + 20, functionCount) ||
        !read32(bytes, exportRva + 24, nameCount) ||
        !read32(bytes, exportRva + 28, functionTable) ||
        !read32(bytes, exportRva + 32, nameTable) ||
        !read32(bytes, exportRva + 36, ordinalTable))
        return fail(error, "truncated IMAGE_EXPORT_DIRECTORY");
    if (!asciiZ(bytes, dllName, 260, image.exportModuleName))
        return fail(error, "invalid PE export module name");
    if (functionCount == 0 || functionCount > 8192 ||
        nameCount > 8192 ||
        !fits(bytes.size(), functionTable, static_cast<std::uint64_t>(functionCount) * 4) ||
        !fits(bytes.size(), nameTable, static_cast<std::uint64_t>(nameCount) * 4) ||
        !fits(bytes.size(), ordinalTable, static_cast<std::uint64_t>(nameCount) * 2) ||
        static_cast<std::uint64_t>(ordinalBase) + functionCount - 1 > 0xffffffffULL)
        return fail(error, "invalid PE export address/name/ordinal tables");

    // Multiple export names may alias the same EAT slot. Preserve every
    // alias while retaining one ordinal for all names of that function.
    std::vector<std::vector<std::string>> names(functionCount);
    for (std::uint32_t i = 0; i < nameCount; ++i) {
        std::uint32_t nameRva = 0;
        std::uint16_t functionIndex = 0;
        std::string name;
        if (!read32(bytes, static_cast<std::uint64_t>(nameTable) + i * 4u, nameRva) ||
            !read16(bytes, static_cast<std::uint64_t>(ordinalTable) + i * 2u,
                    functionIndex) ||
            functionIndex >= functionCount ||
            !asciiZ(bytes, nameRva, 1024, name))
            return fail(error, "invalid PE named export");
        names[functionIndex].push_back(std::move(name));
    }

    const std::uint64_t exportEnd = static_cast<std::uint64_t>(exportRva) + exportSize;
    for (std::uint32_t i = 0; i < functionCount; ++i) {
        std::uint32_t rva = 0;
        if (!read32(bytes, static_cast<std::uint64_t>(functionTable) + i * 4u, rva))
            return fail(error, "truncated PE export function table");
        if (!rva) {
            if (!names[i].empty())
                return fail(error, "named export points to an empty EAT slot");
            continue;
        }
        if (rva >= bytes.size())
            return fail(error, "PE export target outside image");
        Export entry;
        entry.ordinal = ordinalBase + i;
        entry.rva = rva;
        if (rva >= exportRva && rva < exportEnd) {
            if (!asciiZ(bytes, rva, 1024, entry.forwarder) ||
                entry.forwarder.find('.') == std::string::npos)
                return fail(error, "invalid PE forwarded export");
            // A forwarder must be terminated inside its directory.
            if (static_cast<std::uint64_t>(rva) + entry.forwarder.size() >= exportEnd)
                return fail(error, "forwarded export exceeds export directory");
        }
        if (names[i].empty()) {
            image.exports.push_back(std::move(entry));
        } else {
            for (const auto& name : names[i]) {
                Export alias = entry;
                alias.name = name;
                image.exports.push_back(std::move(alias));
            }
        }
    }
    return true;
}

} // namespace

bool load(const std::vector<std::uint8_t>& file,
          std::uint32_t requestedBase,
          Image& output,
          std::string& error,
          bool allowDll) {
    error.clear();
    output = Image{};

    std::uint16_t mz = 0;
    std::uint32_t peOffset = 0;
    if (!read16(file, 0, mz) || mz != 0x5a4d ||
        !read32(file, 0x3c, peOffset))
        return fail(error, "invalid DOS MZ header");

    std::uint32_t signature = 0;
    std::uint16_t machine = 0, sectionCount = 0, optionalSize = 0;
    std::uint16_t fileFlags = 0;
    if (!fits(file.size(), peOffset, 24) ||
        !read32(file, peOffset, signature) || signature != 0x00004550 ||
        !read16(file, static_cast<std::uint64_t>(peOffset) + 4, machine) ||
        !read16(file, static_cast<std::uint64_t>(peOffset) + 6, sectionCount) ||
        !read16(file, static_cast<std::uint64_t>(peOffset) + 20, optionalSize) ||
        !read16(file, static_cast<std::uint64_t>(peOffset) + 22, fileFlags))
        return fail(error, "invalid PE/COFF header");

    const bool isDll = (fileFlags & 0x2000u) != 0;
    if (machine != kMachineI386 || (fileFlags & 0x0002u) == 0 ||
        isDll != allowDll)
        return fail(error, "expected an executable i386 PE32 image of the requested EXE/DLL type");
    if (sectionCount == 0 || sectionCount > 96)
        return fail(error, "invalid section count");

    const std::uint64_t optional = static_cast<std::uint64_t>(peOffset) + 24;
    const std::uint64_t sectionTable = optional + optionalSize;
    if (optionalSize < 96 || !fits(file.size(), optional, optionalSize) ||
        !fits(file.size(), sectionTable, static_cast<std::uint64_t>(sectionCount) * 40))
        return fail(error, "truncated PE optional header or section table");

    std::uint16_t magic = 0;
    std::uint32_t entryRva = 0, preferredBase = 0, sizeOfImage = 0;
    std::uint32_t headersSize = 0, directoryCount = 0;
    if (!read16(file, optional, magic) || magic != kPe32Magic ||
        !read32(file, optional + 16, entryRva) ||
        !read32(file, optional + 28, preferredBase) ||
        !read32(file, optional + 56, sizeOfImage) ||
        !read32(file, optional + 60, headersSize) ||
        !read32(file, optional + 92, directoryCount))
        return fail(error, "invalid PE32 optional header");

    const std::uint32_t base = requestedBase ? requestedBase : preferredBase;
    if (sizeOfImage == 0 || sizeOfImage > kMaxImageSize ||
        headersSize == 0 || headersSize > sizeOfImage ||
        !fits(file.size(), 0, headersSize) ||
        static_cast<std::uint64_t>(base) + sizeOfImage > 0x100000000ULL ||
        entryRva >= sizeOfImage)
        return fail(error, "invalid image size, load address or entry point");
    if (sectionTable + static_cast<std::uint64_t>(sectionCount) * 40 > headersSize)
        return fail(error, "section headers extend beyond SizeOfHeaders");

    std::uint32_t exportRva = 0, exportSize = 0;
    std::uint32_t importRva = 0, importSize = 0, relocRva = 0, relocSize = 0;
    std::uint32_t tlsRva = 0, tlsSize = 0, delayRva = 0, delaySize = 0;
    if (!directory(file, optional, optionalSize, directoryCount, 0, exportRva, exportSize) ||
        !directory(file, optional, optionalSize, directoryCount, 1, importRva, importSize) ||
        !directory(file, optional, optionalSize, directoryCount, 5, relocRva, relocSize) ||
        !directory(file, optional, optionalSize, directoryCount, 9, tlsRva, tlsSize) ||
        !directory(file, optional, optionalSize, directoryCount, 13, delayRva, delaySize))
        return fail(error, "truncated PE data directory");
    if (delayRva || delaySize)
        return fail(error, "delay-loaded PE imports are not supported yet");
    if ((tlsRva == 0) != (tlsSize == 0) ||
        (tlsRva && (tlsSize < 24 || !fits(sizeOfImage, tlsRva, tlsSize))))
        return fail(error, "invalid PE TLS directory range");

    Image image;
    image.isDll = isDll;
    image.preferredBase = preferredBase;
    image.loadBase = base;
    image.entryPoint = entryRva ? base + entryRva : 0; // DLLs may omit DllMain
    image.sizeOfHeaders = headersSize;
    image.bytes.resize(sizeOfImage, 0);
    std::copy_n(file.begin(), headersSize, image.bytes.begin());

    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    bool executableEntry = false;
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        const std::uint64_t at = sectionTable + i * 40;
        std::uint32_t virtualSize = 0, rva = 0, rawSize = 0, rawOffset = 0;
        std::uint32_t flags = 0;
        if (!read32(file, at + 8, virtualSize) ||
            !read32(file, at + 12, rva) ||
            !read32(file, at + 16, rawSize) ||
            !read32(file, at + 20, rawOffset) ||
            !read32(file, at + 36, flags))
            return fail(error, "truncated section header");

        const std::uint32_t mappedSize = std::max(virtualSize, rawSize);
        if (mappedSize == 0 || rva < headersSize ||
            !fits(sizeOfImage, rva, mappedSize) ||
            (rawSize && !fits(file.size(), rawOffset, rawSize)))
            return fail(error, "section exceeds file or guest image bounds");
        for (const auto& range : ranges) {
            if (rva < range.second && range.first < static_cast<std::uint64_t>(rva) + mappedSize)
                return fail(error, "overlapping PE sections");
        }
        ranges.emplace_back(rva, static_cast<std::uint64_t>(rva) + mappedSize);

        Section section;
        for (unsigned n = 0; n < 8 && file[at + n]; ++n)
            section.name.push_back(static_cast<char>(file[at + n]));
        section.rva = rva;
        section.virtualSize = virtualSize;
        section.rawSize = rawSize;
        section.characteristics = flags;
        image.sections.push_back(std::move(section));
        if (rawSize)
            std::copy_n(file.begin() + rawOffset, rawSize, image.bytes.begin() + rva);
        if ((flags & kExecutableSection) && entryRva >= rva &&
            static_cast<std::uint64_t>(entryRva) < static_cast<std::uint64_t>(rva) + mappedSize)
            executableEntry = true;
    }
    if (!executableEntry && !(isDll && entryRva == 0))
        return fail(error, "entry point is not in an executable section");

    if (base != preferredBase) {
        if (!relocRva || !relocSize || !fits(image.bytes.size(), relocRva, relocSize))
            return fail(error, "image cannot be rebased: relocation directory missing");
        const std::int64_t delta = static_cast<std::int64_t>(base) - preferredBase;
        const std::uint64_t end = static_cast<std::uint64_t>(relocRva) + relocSize;
        std::uint64_t at = relocRva;
        while (at < end) {
            std::uint32_t pageRva = 0, blockSize = 0;
            if (end - at < 8 || !read32(image.bytes, at, pageRva) ||
                !read32(image.bytes, at + 4, blockSize) ||
                blockSize < 8 || (blockSize & 1) || blockSize > end - at)
                return fail(error, "invalid base relocation block");
            for (std::uint64_t item = at + 8; item < at + blockSize; item += 2) {
                std::uint16_t entry = 0;
                read16(image.bytes, item, entry);
                const std::uint32_t type = entry >> 12;
                if (type == 0) continue; // IMAGE_REL_BASED_ABSOLUTE padding
                if (type != 3)
                    return fail(error, "unsupported relocation type (expected HIGHLOW)");
                const std::uint64_t target = static_cast<std::uint64_t>(pageRva) + (entry & 0x0fff);
                std::uint32_t oldValue = 0;
                if (!read32(image.bytes, target, oldValue))
                    return fail(error, "relocation target outside image");
                const std::int64_t relocated = static_cast<std::int64_t>(oldValue) + delta;
                if (relocated < 0 || relocated > std::numeric_limits<std::uint32_t>::max())
                    return fail(error, "relocation value exceeds 32-bit address space");
                write32(image.bytes, static_cast<std::uint32_t>(target),
                        static_cast<std::uint32_t>(relocated));
            }
            at += blockSize;
        }
    }

    // IMAGE_TLS_DIRECTORY32 stores absolute guest virtual addresses.
    // Relocations above have already adjusted AddressOfCallbacks.
    // Record callbacks for a future x86 runtime, but NEVER execute them
    // in this diagnostic PE linking tool.
    if (tlsRva) {
        std::uint32_t callbackVa = 0;
        if (!read32(image.bytes, static_cast<std::uint64_t>(tlsRva) + 12, callbackVa))
            return fail(error, "truncated IMAGE_TLS_DIRECTORY32");
        if (callbackVa) {
            if (callbackVa < base ||
                !fits(image.bytes.size(), static_cast<std::uint64_t>(callbackVa) - base, 4))
                return fail(error, "TLS callback array outside PE image");
            const std::uint64_t callbackRva = static_cast<std::uint64_t>(callbackVa) - base;
            bool terminated = false;
            for (unsigned i = 0; i < 1024; ++i) {
                std::uint32_t address = 0;
                if (!read32(image.bytes, callbackRva + static_cast<std::uint64_t>(i) * 4, address))
                    return fail(error, "unterminated TLS callback array");
                if (!address) { terminated = true; break; }
                if (address < base || address - base >= image.bytes.size())
                    return fail(error, "TLS callback target outside PE image");
                image.tlsCallbacks.push_back(address);
            }
            if (!terminated) return fail(error, "too many TLS callbacks");
        }
    }

    if (!parseImports(image, importRva, importSize, error) ||
        !parseExports(image, exportRva, exportSize, error))
        return false;
    output = std::move(image);
    return true;
}

bool bindImports(Image& image, const ImportResolver& resolver, std::string& error) {
    error.clear();
    if (image.imports.empty()) return true;
    if (!resolver)
        return fail(error, "PE import resolver is not configured");

    // Resolve all symbols before changing any IAT entry. A failed DLL or
    // export must not leave a half-patched guest image behind.
    std::vector<std::uint32_t> resolved;
    resolved.reserve(image.imports.size());
    for (const auto& imported : image.imports) {
        std::uint32_t address = 0;
        if (!fits(image.bytes.size(), imported.iatRva, 4))
            return fail(error, "import IAT slot outside guest image");
        if (!resolver(imported, address) || !address) {
            error = "unresolved Windows import: " + imported.dll + "!" +
                    (imported.byOrdinal ? ("#" + std::to_string(imported.ordinal))
                                        : imported.name);
            return false;
        }
        resolved.push_back(address);
    }
    for (std::size_t i = 0; i < image.imports.size(); ++i)
        write32(image.bytes, image.imports[i].iatRva, resolved[i]);
    return true;
}

} // namespace pe32
} // namespace emuwasm
