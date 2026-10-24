// End-to-end CLI regression: execute a synthetic on-disk PE32 entry point
// through the actual headless runner and stop before a DLL call is dispatched.
// No real Windows DLL code or copyrighted game binary is used by this test.
#include "pe32_fixture.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;

bool save(const fs::path& file, const std::vector<std::uint8_t>& data) {
    std::ofstream out(file, std::ios::binary);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const fs::path runner = fs::absolute(argv[1]);
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("emuwasm-cli-test-" + std::to_string(nonce));
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) return 3;

    if (!save(root / "app.exe", emuwasm::test::makePe32WithImports()) ||
        !save(root / "kernel32.dll", emuwasm::test::makePe32DllWithImports()) ||
        !save(root / "ntdll.dll",
              emuwasm::test::makePe32Dll("ntdll.dll", "RtlGetTickCount"))) {
        fs::remove_all(root, ec);
        return 4;
    }

    const fs::path output = root / "output.log";
    // All values below are generated test paths, never untrusted user input.
    const std::string command =
        "\"" + runner.string() + "\" --no-game-dir --wine-dir \"" +
        root.string() + "\" --max-steps 30 \"" +
        (root / "app.exe").string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());

    std::ifstream file(output);
    const std::string text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    std::printf("Guest runner output:\n%s\n", text.c_str());
    fs::remove_all(root, ec);

    // The first instruction is CALL DWORD PTR [kernel32.GetTickCount].
    // EIP transfers from app.exe to the synthetic DLL after ONE x86 step.
    if (status != 0 ||
        text.find("Linked 1 EXE imports and 2 PE DLLs") == std::string::npos ||
        text.find("STOP: reached kernel32.dll") == std::string::npos ||
        text.find("after 1 x86 instructions") == std::string::npos)
        return 5;
    return 0;
}
