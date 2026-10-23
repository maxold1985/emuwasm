#include "emuwasm/probe.h"
#include <iostream>
#include <string_view>
int main() {
    std::cout << emuwasm::status() << '\n';
    return std::string_view(emuwasm::status()).empty() ? 1 : 0;
}
