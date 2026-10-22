#pragma once
#include "machine.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>

namespace nexrt::rv {
/// Checked ELF32 little-endian RISC-V load and global/weak diagnostic symbols.
/// Program segments initialize RAM as a trusted boot operation, outside PMP.
struct Image {
    uint32_t entry=0;
    struct Symbol { uint32_t address=0,size=0; };
    std::unordered_map<std::string,Symbol> symbols;
    explicit Image(const std::filesystem::path& path,Machine& machine);
};
}
