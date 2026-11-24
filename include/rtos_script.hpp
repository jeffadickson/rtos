#pragma once
/**
 * @file rtos_script.hpp
 * @brief Zero-heap, fuel-bounded 32-bit diagnostic bytecode VM (RTOS_SCRIPT).
 *
 * Allows engineers to run lightweight CRC-32C verified diagnostic scripts (.nxs)
 * without rebuilding firmware. Strictly bounded by instruction fuel, private
 * static scratchpad memory, and explicit board syscall allowlists. Defaults OFF;
 * developer may opt in on any bench or flight build.
 */

#include "rtos_config.hpp"

#include <stddef.h>
#include <stdint.h>

namespace rtos::script {

constexpr uint32_t package_magic = 0x3153584Eu; // "NXS1"
constexpr uint16_t package_version = 1u;
constexpr size_t register_count = 16;
constexpr size_t scratch_words = 64;
constexpr uint32_t max_code_words = 1024;

/// Package capability flags requested by compiled .nxs script.
constexpr uint16_t flag_allow_poke = 0x0001u;
constexpr uint16_t flag_allow_vault_write = 0x0002u;

/// 20-byte binary header preceding little-endian 32-bit instruction words.
struct PackageHeader {
    uint32_t magic{};
    uint16_t version{};
    uint16_t flags{};
    uint32_t code_words{};
    uint32_t max_fuel{};
    uint32_t crc32c{};
};

static_assert(sizeof(PackageHeader) == 20, "script::PackageHeader must be 20 bytes");

/// Fixed 32-bit VM instruction opcodes.
enum Opcode : uint8_t {
    op_halt  = 0x00u,
    op_movi  = 0x01u,
    op_lui   = 0x02u,
    op_ori   = 0x03u,
    op_mov   = 0x04u,
    op_add   = 0x05u,
    op_sub   = 0x06u,
    op_mul   = 0x07u,
    op_div   = 0x08u,
    op_and   = 0x09u,
    op_or    = 0x0Au,
    op_xor   = 0x0Bu,
    op_shl   = 0x0Cu,
    op_shr   = 0x0Du,
    op_addi  = 0x0Eu,
    op_jmp   = 0x10u,
    op_beq   = 0x11u,
    op_bne   = 0x12u,
    op_blt   = 0x13u,
    op_bge   = 0x14u,
    op_load  = 0x18u,
    op_store = 0x19u,
    op_sys   = 0x20u,
};

/// Allowlisted diagnostic syscall IDs invoked via op_sys.
enum SyscallId : uint16_t {
    sys_sensor      = 1u,
    sys_peek        = 2u,
    sys_poke        = 3u,
    sys_sleep       = 4u,
    sys_log         = 5u,
    sys_vault_read  = 6u,
    sys_vault_write = 7u,
    sys_emit        = 8u,
};

/// Terminal execution status of a VM run.
enum class Status : uint8_t {
    halted = 0,
    invalid_package = 1,
    bad_crc = 2,
    fuel_exhausted = 3,
    pc_out_of_bounds = 4,
    illegal_opcode = 5,
    div_by_zero = 6,
    mem_fault = 7,
    syscall_denied = 8,
};

/// Encode an immediate-format instruction: [31:24]=op, [23:20]=rd, [19:16]=ra, [15:0]=imm16.
constexpr uint32_t encode_imm(uint8_t op, uint8_t rd, uint8_t ra, int16_t imm) {
    return (static_cast<uint32_t>(op) << 24) |
           (static_cast<uint32_t>(rd & 0xfu) << 20) |
           (static_cast<uint32_t>(ra & 0xfu) << 16) |
           static_cast<uint32_t>(static_cast<uint16_t>(imm));
}

/// Encode a 3-register instruction: [31:24]=op, [23:20]=rd, [19:16]=ra, [15:12]=rb.
constexpr uint32_t encode_reg(uint8_t op, uint8_t rd, uint8_t ra, uint8_t rb) {
    return (static_cast<uint32_t>(op) << 24) |
           (static_cast<uint32_t>(rd & 0xfu) << 20) |
           (static_cast<uint32_t>(ra & 0xfu) << 16) |
           (static_cast<uint32_t>(rb & 0xfu) << 12);
}

/// Host/board syscall policy and allowlist callbacks.
struct HostPolicy {
    uint32_t max_fuel_cap{4096};
    bool allow_poke{false};
    bool allow_vault_write{false};

    /// Return true if syscall succeeded (writing result to out_val); false denies and halts VM.
    bool (*on_syscall)(SyscallId id, uint32_t arg0, uint32_t arg1, uint32_t& out_val, void* ctx){};
    void* context{};
};

/// Result returned by Vm::execute().
struct RunResult {
    Status status{Status::invalid_package};
    int32_t return_value{};
    uint32_t instructions_executed{};
    uint32_t final_pc{};
    uint32_t remaining_fuel{};
};

/// Compute IEEE/Castagnoli CRC-32C over a byte span.
uint32_t crc32c(const uint8_t* data, size_t len);

/// Serialize a valid .nxs binary package (header + little-endian code words + CRC-32C).
/// Returns total bytes written, or 0 if output buffer is too small or inputs are invalid.
size_t build_package(const uint32_t* code, uint32_t code_words, uint32_t max_fuel,
                     uint16_t flags, uint8_t* out_buf, size_t out_capacity);

/// Zero-heap fuel-bounded sandboxed diagnostic bytecode VM.
class Vm {
public:
    Vm() = default;

    /// Validate a raw .nxs byte package without executing it.
    static Status validate(const uint8_t* pkg, size_t pkg_bytes, PackageHeader& out_header);

    /// Validate and execute a .nxs package under the given host policy.
    RunResult execute(const uint8_t* pkg, size_t pkg_bytes, const HostPolicy& policy);

    /// Read final register state after execute() (0..15).
    uint32_t reg(size_t index) const { return index < register_count ? regs_[index] : 0u; }

    /// Read private scratchpad word after execute() (0..63).
    uint32_t scratch(size_t index) const { return index < scratch_words ? scratch_[index] : 0u; }

private:
    uint32_t regs_[register_count]{};
    uint32_t scratch_[scratch_words]{};
};

} // namespace rtos::script
