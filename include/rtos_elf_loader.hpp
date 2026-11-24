#pragma once
/**
 * @file rtos_elf_loader.hpp
 * @brief Optional static-arena ELF32 ET_REL dynamic module loader (RTOS_ELF_LOADER).
 *
 * Defaults OFF in minimal and flight presets; developer may opt in on any build.
 * Fulfills NXR-146 / NXR-147: validates ELF32 relocatable objects in a caller-owned
 * static arena, binds external symbols strictly against an allowlisted ExportSymbol[]
 * table (fail-closed rejection of malloc or unapproved symbols), relocates RV32IM and
 * ARMv7-M code/data, and requires a cache-publication (sync_icache) hook before
 * transitioning the module slot to SlotState::ready.
 */

#include "rtos_config.hpp"

#include <stddef.h>
#include <stdint.h>

namespace rtos::elf {

/// Maximum section headers supported in a single static-arena relocatable ELF32 object.
constexpr size_t max_sections = 32;

/// Explicit module slot lifecycle states (NXR-146).
enum class SlotState : uint8_t {
    empty = 0,
    loading = 1,
    ready = 2,
    faulted = 3,
};

/// Supported ELF32 target machines.
enum class TargetArch : uint16_t {
    any = 0,
    arm = 40,    ///< EM_ARM (ARMv7-M / Thumb-2)
    riscv = 243, ///< EM_RISCV (RV32IM)
};

/// Explicit failure/success codes returned by Loader::load().
enum class LoadStatus : uint8_t {
    ok = 0,
    invalid_argument = 1,
    missing_icache_hook = 2,
    bad_elf_header = 3,
    unsupported_machine = 4,
    bad_section_table = 5,
    arena_too_small = 6,
    unresolved_symbol = 7,
    abi_mismatch = 8,
    unsupported_reloc = 9,
    reloc_out_of_range = 10,
    entry_not_found = 11,
};

/// Allowlisted host/kernel symbol exported to dynamically loaded ELF32 modules.
struct ExportSymbol {
    const char* name{};
    uintptr_t address{};
    uint16_t abi_version{1};
};

/// Mandatory cache-maintenance / memory-barrier publication callback (NXR-147).
using SyncIcacheFn = void (*)(uintptr_t addr, size_t bytes, void* ctx);

/// Configuration for one static-arena ELF32 load operation.
struct LoadConfig {
    TargetArch expected_arch{TargetArch::any};
    uint16_t required_abi_version{1};
    const ExportSymbol* exports{};
    size_t export_count{};
    SyncIcacheFn sync_icache{};
    void* sync_ctx{};
    const char* entry_symbol{"module_entry"};
};

/// Loaded module descriptor produced by Loader::load().
struct LoadedModule {
    SlotState state{SlotState::empty};
    TargetArch arch{TargetArch::any};
    uintptr_t arena_base{};
    size_t arena_used{};
    uintptr_t entry_address{};
    uint32_t relocations_applied{};
    uint32_t sections_loaded{};
};

/// Zero-heap static-arena ELF32 ET_REL loader and relocator.
class Loader {
public:
    Loader() = default;

    /// Validate, place, relocate, and publish an ELF32 ET_REL image into caller-owned static arena.
    LoadStatus load(const uint8_t* elf_bytes, size_t elf_size,
                    uint8_t* arena, size_t arena_bytes,
                    const LoadConfig& config,
                    LoadedModule& out_module);

    /// Unload a module slot cleanly, zeroing its arena span if requested.
    static void unload(LoadedModule& mod, uint8_t* arena = nullptr, size_t arena_bytes = 0);
};

} // namespace rtos::elf
