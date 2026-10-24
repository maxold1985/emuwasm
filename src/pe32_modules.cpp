#include "emuwasm/pe32_modules.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <utility>

namespace emuwasm {
namespace pe32 {
namespace {

// Windows module lookup is case-insensitive; export symbol names are NOT.
std::string moduleKey(std::string name) {
    if (name.empty() || name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos)
        return {};
    for (char& c : name)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.size() < 4 || name.compare(name.size() - 4, 4, ".dll") != 0)
        name += ".dll";
    return name;
}

bool parseForwardedOrdinal(const std::string& symbol, std::uint32_t& value) {
    if (symbol.size() < 2 || symbol[0] != '#') return false;
    std::uint32_t number = 0;
    for (std::size_t i = 1; i < symbol.size(); ++i) {
        const char c = symbol[i];
        if (c < '0' || c > '9') return false;
        number = number * 10 + static_cast<unsigned>(c - '0');
        if (number > 65535) return false;
    }
    value = number;
    return true;
}
} // namespace

bool ModuleRegistry::addDll(std::string filename, Image image, std::string& error) {
    error.clear();
    const std::string key = moduleKey(std::move(filename));
    if (key.empty() || !image.isDll || image.bytes.empty()) {
        error = "expected a parsed PE32 DLL image and valid module filename";
        return false;
    }
    if (findDll(key)) {
        error = "duplicate guest DLL: " + key;
        return false;
    }
    const std::uint64_t start = image.loadBase;
    const std::uint64_t end = start + image.bytes.size();
    for (const auto& existing : modules_) {
        const std::uint64_t otherStart = existing.image.loadBase;
        const std::uint64_t otherEnd = otherStart + existing.image.bytes.size();
        if (start < otherEnd && otherStart < end) {
            error = "overlapping guest DLL address ranges";
            return false;
        }
    }
    modules_.push_back({key, std::move(image)});
    return true;
}

const Image* ModuleRegistry::findDll(const std::string& filename) const {
    const std::string key = moduleKey(filename);
    if (key.empty()) return nullptr;
    for (const auto& module : modules_)
        if (module.key == key) return &module.image;
    return nullptr;
}

bool ModuleRegistry::resolveExport(const std::string& dll,
                                   const std::string& name,
                                   std::uint32_t ordinal, bool byOrdinal,
                                   std::uint32_t& address,
                                   std::vector<std::string>& visited) const {
    if (visited.size() >= 32) return false;
    const std::string key = moduleKey(dll);
    if (key.empty()) return false;
    const std::string symbol = byOrdinal ? ("#" + std::to_string(ordinal)) : name;
    if (symbol.empty()) return false;
    const std::string node = key + "!" + symbol;
    if (std::find(visited.begin(), visited.end(), node) != visited.end())
        return false;
    visited.push_back(node);

    const Image* module = findDll(key);
    if (!module) return false;
    for (const auto& entry : module->exports) {
        if ((byOrdinal && entry.ordinal != ordinal) ||
            (!byOrdinal && entry.name != name))
            continue;
        if (entry.forwarder.empty()) {
            const std::uint64_t target = static_cast<std::uint64_t>(module->loadBase) + entry.rva;
            if (target == 0 || target > 0xffffffffULL) return false;
            address = static_cast<std::uint32_t>(target);
            return true;
        }
        const std::size_t dot = entry.forwarder.rfind('.');
        if (dot == std::string::npos || dot == 0 ||
            dot + 1 >= entry.forwarder.size())
            return false;
        const std::string nextDll = entry.forwarder.substr(0, dot);
        const std::string nextSymbol = entry.forwarder.substr(dot + 1);
        if (nextSymbol[0] == '#') {
            std::uint32_t nextOrdinal = 0;
            if (!parseForwardedOrdinal(nextSymbol, nextOrdinal)) return false;
            return resolveExport(nextDll, "", nextOrdinal, true, address, visited);
        }
        return resolveExport(nextDll, nextSymbol, 0, false, address, visited);
    }
    return false;
}

bool ModuleRegistry::resolve(const Import& imported,
                             std::uint32_t& guestAddress) const {
    guestAddress = 0;
    std::vector<std::string> visited;
    return resolveExport(imported.dll, imported.name, imported.ordinal,
                         imported.byOrdinal, guestAddress, visited);
}

bool ModuleRegistry::linkImports(Image& image, std::string& error) const {
    return bindImports(image,
        [this](const Import& imported, std::uint32_t& address) {
            return resolve(imported, address);
        }, error);
}


std::vector<std::string> ModuleRegistry::dllNames() const {
    std::vector<std::string> names;
    names.reserve(modules_.size());
    for (const auto& module : modules_)
        names.push_back(module.key);
    return names;
}

bool ModuleRegistry::linkAllImports(std::string& error) {
    error.clear();
    struct PendingWrite {
        std::size_t moduleIndex;
        std::uint32_t rva;
        std::uint32_t address;
    };
    std::vector<PendingWrite> writes;
    // Resolve everything first; never partially patch a module set.
    for (std::size_t index = 0; index < modules_.size(); ++index) {
        const auto& module = modules_[index];
        for (const auto& imported : module.image.imports) {
            std::uint32_t address = 0;
            if (static_cast<std::uint64_t>(imported.iatRva) + 4 >
                module.image.bytes.size() ||
                !resolve(imported, address) || !address) {
                error = "unresolved import in " + module.key + ": " +
                        imported.dll + "!" +
                        (imported.byOrdinal ? "#" + std::to_string(imported.ordinal)
                                            : imported.name);
                return false;
            }
            writes.push_back({index, imported.iatRva, address});
        }
    }
    for (const auto& write : writes) {
        auto& bytes = modules_[write.moduleIndex].image.bytes;
        for (unsigned i = 0; i < 4; ++i)
            bytes[write.rva + i] =
                static_cast<std::uint8_t>(write.address >> (8 * i));
    }
    return true;
}

} // namespace pe32
} // namespace emuwasm
