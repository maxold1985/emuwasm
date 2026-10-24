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

} // namespace

bool load(const std::vector<std::uint8_t>& file,
          std::uint32_t requestedBase,
          Image& output,
          std::string& error) {
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

    if (machine != kMachineI386 || (fileFlags & 0x0002u) == 0 ||
        (fileFlags & 0x2000u) != 0)
        return fail(error, "only executable i386 PE32 images are supported");
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

    std::uint32_t importRva = 0, importSize = 0, relocRva = 0, relocSize = 0;
    std::uint32_t tlsRva = 0, tlsSize = 0, delayRva = 0, delaySize = 0;
    if (!directory(file, optional, optionalSize, directoryCount, 1, importRva, importSize) ||
        !directory(file, optional, optionalSize, directoryCount, 5, relocRva, relocSize) ||
        !directory(file, optional, optionalSize, directoryCount, 9, tlsRva, tlsSize) ||
        !directory(file, optional, optionalSize, directoryCount, 13, delayRva, delaySize))
        return fail(error, "truncated PE data directory");
    if (delayRva || delaySize)
        return fail(error, "delay-loaded PE imports are not supported yet");
    if (tlsRva || tlsSize)
        return fail(error, "PE TLS callbacks are not supported yet");

    Image image;
    image.preferredBase = preferredBase;
    image.loadBase = base;
    image.entryPoint = base + entryRva;
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
    if (!executableEntry)
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

    if (!parseImports(image, importRva, importSize, error))
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
