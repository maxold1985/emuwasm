#include "emuwasm/pe32_wine.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace emuwasm {
namespace pe32 {
namespace {

std::string lower(std::string value) {
    for (char& ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string dllKey(const std::string& input) {
    if (input.empty() || input.size() > 260 || input.front() == '.' ||
        input.find("..") != std::string::npos)
        return {};
    for (char ch : input)
        if (!(std::isalnum(static_cast<unsigned char>(ch)) ||
              ch == '_' || ch == '-' || ch == '.'))
            return {};
    std::string key = lower(input);
    if (key.size() < 4 || key.compare(key.size() - 4, 4, ".dll") != 0)
        key += ".dll";
    return key;
}

bool readFile(const std::filesystem::path& path,
              std::vector<std::uint8_t>& bytes, std::string& error) {
    constexpr std::streamoff maxFileSize = 256LL * 1024 * 1024;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        error = "cannot open PE file: " + path.string();
        return false;
    }
    const std::streamoff length = stream.tellg();
    if (length <= 0 || length > maxFileSize) {
        error = "PE file empty or larger than 256 MiB: " + path.string();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(length));
    stream.seekg(0, std::ios::beg);
    if (!stream.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()))) {
        error = "cannot read PE file: " + path.string();
        return false;
    }
    return true;
}

bool forwardedOrdinal(const std::string& symbol, std::uint32_t& number) {
    if (symbol.size() < 2 || symbol.front() != '#') return false;
    number = 0;
    for (std::size_t i = 1; i < symbol.size(); ++i) {
        if (symbol[i] < '0' || symbol[i] > '9') return false;
        number = number * 10 + static_cast<unsigned>(symbol[i] - '0');
        if (number > 65535) return false;
    }
    return true;
}
} // namespace

void WineDllLoader::addSearchDirectory(const std::filesystem::path& directory) {
    if (!directory.empty())
        directories_.push_back(directory);
}

bool WineDllLoader::findDllPath(const std::string& filename,
                                std::filesystem::path& path) const {
    const std::string key = dllKey(filename);
    if (key.empty()) return false;
    for (const auto& directory : directories_) {
        // First try an exact spelling; then handle case-sensitive host FS
        // with Wine's case-insensitive Windows module name rules.
        std::error_code ec;
        const auto direct = directory / key;
        if (std::filesystem::is_regular_file(direct, ec)) {
            path = direct;
            return true;
        }
        ec.clear();
        std::filesystem::directory_iterator it(directory, ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            if (lower(it->path().filename().string()) == key &&
                it->is_regular_file(ec) && !ec) {
                path = it->path();
                return true;
            }
        }
    }
    return false;
}

bool WineDllLoader::available(std::uint64_t start,
                              std::uint64_t length) const {
    if (start < 0x10000 || !length || start + length > 0x100000000ULL)
        return false;
    const std::uint64_t end = start + length;
    for (const auto& range : reserved_)
        if (start < range.second && range.first < end)
            return false;
    for (const auto& name : modules_.dllNames()) {
        const Image* dll = modules_.findDll(name);
        if (dll && start < static_cast<std::uint64_t>(dll->loadBase) +
                               dll->bytes.size() &&
            dll->loadBase < end)
            return false;
    }
    return true;
}

bool WineDllLoader::loadExecutable(const std::filesystem::path& path,
                                   Image& image, std::string& error) {
    error.clear();
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes, error)) return false;
    Image parsed;
    if (!load(bytes, 0, parsed, error, false)) {
        error = path.string() + ": " + error;
        return false;
    }
    if (!available(parsed.loadBase, parsed.bytes.size())) {
        error = "EXE image address overlaps an existing guest module";
        return false;
    }
    reserved_.push_back({parsed.loadBase,
                         static_cast<std::uint64_t>(parsed.loadBase) +
                             parsed.bytes.size()});
    image = std::move(parsed);
    return true;
}

bool WineDllLoader::loadDll(const std::string& filename, std::string& error) {
    error.clear();
    return loadDllRecursive(filename, error, 0);
}

bool WineDllLoader::loadDllRecursive(const std::string& filename,
                                     std::string& error, unsigned depth) {
    if (depth > 64 || modules_.dllNames().size() >= 128) {
        error = "PE DLL dependency limit exceeded";
        return false;
    }
    const std::string key = dllKey(filename);
    if (key.empty()) {
        error = "invalid Windows DLL name: " + filename;
        return false;
    }
    if (modules_.findDll(key)) return true; // also breaks circular imports

    std::filesystem::path path;
    if (!findDllPath(key, path)) {
        error = "missing Wine i386 PE DLL: " + key +
                " (add its i386-windows directory)";
        return false;
    }
    std::vector<std::uint8_t> file;
    if (!readFile(path, file, error)) return false;
    Image parsed;
    if (!load(file, 0, parsed, error, true)) {
        error = path.string() + ": " + error;
        return false;
    }

    // Prefer the image's requested Windows base if free. If occupied,
    // allocate a new guest range and let the PE HIGHLOW relocator rebase it.
    std::uint64_t base = parsed.preferredBase;
    if (!available(base, parsed.bytes.size())) {
        base = (nextBase_ + 0xffffULL) & ~0xffffULL;
        while (!available(base, parsed.bytes.size()) &&
               base + parsed.bytes.size() <= 0xf0000000ULL)
            base += 0x10000ULL;
        if (base + parsed.bytes.size() > 0xf0000000ULL) {
            error = "guest address space exhausted while loading " + key;
            return false;
        }
    }
    if (base != parsed.preferredBase &&
        !load(file, static_cast<std::uint32_t>(base), parsed, error, true)) {
        error = path.string() + ": " + error;
        return false;
    }
    if (!modules_.addDll(key, std::move(parsed), error)) return false;
    const Image* loaded = modules_.findDll(key);
    if (!loaded) {
        error = "internal error registering DLL: " + key;
        return false;
    }
    nextBase_ = std::max(nextBase_, (base + loaded->bytes.size() + 0xffffULL) &
                                    ~0xffffULL);

    // Copy dependency names: registering another module may reallocate
    // the registry and invalidate pointers to this Image.
    std::vector<std::string> dependencies;
    for (const auto& imported : loaded->imports) {
        if (std::find(dependencies.begin(), dependencies.end(), imported.dll) ==
            dependencies.end())
            dependencies.push_back(imported.dll);
    }
    for (const auto& dependency : dependencies) {
        if (!loadDllRecursive(dependency, error, depth + 1)) {
            error = key + " -> " + error;
            return false;
        }
    }
    return true;
}

bool WineDllLoader::ensureForwarder(const Import& imported,
                                    std::string& error, unsigned depth) {
    if (depth > 32) {
        error = "PE forwarded export dependency chain exceeds 32 levels";
        return false;
    }
    if (!loadDllRecursive(imported.dll, error, depth)) return false;
    const Image* module = modules_.findDll(imported.dll);
    if (!module) return false;
    std::string forwarder;
    for (const auto& exported : module->exports) {
        if ((imported.byOrdinal && exported.ordinal == imported.ordinal) ||
            (!imported.byOrdinal && exported.name == imported.name)) {
            forwarder = exported.forwarder;
            break;
        }
    }
    if (forwarder.empty()) return true; // normal export, or missing (link reports it)
    const std::size_t dot = forwarder.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= forwarder.size()) {
        error = "malformed forwarded export: " + forwarder;
        return false;
    }
    Import target;
    target.dll = forwarder.substr(0, dot);
    target.name = forwarder.substr(dot + 1);
    if (target.name.front() == '#') {
        std::uint32_t ordinal = 0;
        if (!forwardedOrdinal(target.name, ordinal)) {
            error = "invalid forwarded export ordinal: " + forwarder;
            return false;
        }
        target.byOrdinal = true;
        target.ordinal = static_cast<std::uint16_t>(ordinal);
        target.name.clear();
    }
    return ensureForwarder(target, error, depth + 1);
}

bool WineDllLoader::linkExecutable(Image& exe, std::string& error) {
    error.clear();
    if (exe.isDll || exe.bytes.empty()) {
        error = "linkExecutable expects a parsed PE32 EXE";
        return false;
    }
    // First load direct EXE imports. Loading DLLs recursively adds imports
    // of those DLLs; forwarded exports are loaded only when actually used.
    for (const auto& imported : exe.imports)
        if (!loadDllRecursive(imported.dll, error, 0))
            return false;

    for (const auto& imported : exe.imports)
        if (!ensureForwarder(imported, error, 0))
            return false;

    std::size_t index = 0;
    while (true) {
        const auto names = modules_.dllNames();
        if (index >= names.size()) break;
        const Image* module = modules_.findDll(names[index++]);
        if (!module) {
            error = "internal DLL registry lookup failure";
            return false;
        }
        const std::vector<Import> imports = module->imports;
        for (const auto& imported : imports)
            if (!ensureForwarder(imported, error, 0))
                return false;
    }

    // Stage EXE changes; only publish them after every module IAT succeeds.
    Image patchedExe = exe;
    if (!modules_.linkImports(patchedExe, error) ||
        !modules_.linkAllImports(error))
        return false;
    exe = std::move(patchedExe);
    return true;
}

} // namespace pe32
} // namespace emuwasm
