#include "rtos_script.hpp"

namespace rtos::script {
namespace {

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

void write_u16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
}

void write_u32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xffu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
}

uint32_t crc32c_update(uint32_t crc, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            const uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0x82F63B78u & mask);
        }
    }
    return crc;
}

} // namespace

uint32_t crc32c(const uint8_t* data, size_t len) {
    if (!data && len > 0) return 0;
    return ~crc32c_update(0xffffffffu, data, len);
}

size_t build_package(const uint32_t* code, uint32_t code_words, uint32_t max_fuel,
                     uint16_t flags, uint8_t* out_buf, size_t out_capacity) {
    if (!code || !out_buf || code_words == 0 || code_words > max_code_words || max_fuel == 0) {
        return 0;
    }
    const size_t total_bytes = sizeof(PackageHeader) + static_cast<size_t>(code_words) * 4u;
    if (out_capacity < total_bytes) return 0;

    write_u32(out_buf + 0, package_magic);
    write_u16(out_buf + 4, package_version);
    write_u16(out_buf + 6, flags);
    write_u32(out_buf + 8, code_words);
    write_u32(out_buf + 12, max_fuel);
    write_u32(out_buf + 16, 0u);

    for (uint32_t i = 0; i < code_words; ++i) {
        write_u32(out_buf + sizeof(PackageHeader) + static_cast<size_t>(i) * 4u, code[i]);
    }

    // Hash header prefix [0..16) followed by all code bytes [20..total_bytes).
    uint32_t crc = 0xffffffffu;
    crc = crc32c_update(crc, out_buf, 16u);
    crc = crc32c_update(crc, out_buf + sizeof(PackageHeader), static_cast<size_t>(code_words) * 4u);
    write_u32(out_buf + 16, ~crc);
    return total_bytes;
}

Status Vm::validate(const uint8_t* pkg, size_t pkg_bytes, PackageHeader& out_header) {
    out_header = PackageHeader{};
    if (!pkg || pkg_bytes < sizeof(PackageHeader)) return Status::invalid_package;

    PackageHeader hdr{};
    hdr.magic = read_u32(pkg + 0);
    hdr.version = read_u16(pkg + 4);
    hdr.flags = read_u16(pkg + 6);
    hdr.code_words = read_u32(pkg + 8);
    hdr.max_fuel = read_u32(pkg + 12);
    hdr.crc32c = read_u32(pkg + 16);

    if (hdr.magic != package_magic || hdr.version != package_version) {
        return Status::invalid_package;
    }
    if (hdr.code_words == 0 || hdr.code_words > max_code_words || hdr.max_fuel == 0) {
        return Status::invalid_package;
    }
    const size_t expected_bytes = sizeof(PackageHeader) + static_cast<size_t>(hdr.code_words) * 4u;
    if (pkg_bytes != expected_bytes) {
        return Status::invalid_package;
    }

    uint32_t crc = 0xffffffffu;
    crc = crc32c_update(crc, pkg, 16u);
    crc = crc32c_update(crc, pkg + sizeof(PackageHeader), static_cast<size_t>(hdr.code_words) * 4u);
    if (~crc != hdr.crc32c) {
        return Status::bad_crc;
    }

    out_header = hdr;
    return Status::halted;
}

RunResult Vm::execute(const uint8_t* pkg, size_t pkg_bytes, const HostPolicy& policy) {
    RunResult res{};
    for (size_t i = 0; i < register_count; ++i) regs_[i] = 0;
    for (size_t i = 0; i < scratch_words; ++i) scratch_[i] = 0;

    PackageHeader hdr{};
    const Status vstatus = validate(pkg, pkg_bytes, hdr);
    if (vstatus != Status::halted) {
        res.status = vstatus;
        return res;
    }

    if ((hdr.flags & flag_allow_poke) != 0 && !policy.allow_poke) {
        res.status = Status::syscall_denied;
        return res;
    }
    if ((hdr.flags & flag_allow_vault_write) != 0 && !policy.allow_vault_write) {
        res.status = Status::syscall_denied;
        return res;
    }

    uint32_t fuel = hdr.max_fuel;
    if (policy.max_fuel_cap < fuel) fuel = policy.max_fuel_cap;

    const uint8_t* code_base = pkg + sizeof(PackageHeader);
    uint32_t pc = 0;

    for (;;) {
        if (pc >= hdr.code_words) {
            res.status = Status::pc_out_of_bounds;
            res.final_pc = pc;
            res.remaining_fuel = fuel;
            return res;
        }
        if (fuel == 0) {
            res.status = Status::fuel_exhausted;
            res.final_pc = pc;
            res.remaining_fuel = 0;
            return res;
        }
        --fuel;
        ++res.instructions_executed;

        const uint32_t insn = read_u32(code_base + static_cast<size_t>(pc) * 4u);
        const uint8_t op = static_cast<uint8_t>((insn >> 24) & 0xffu);
        const uint8_t rd = static_cast<uint8_t>((insn >> 20) & 0xfu);
        const uint8_t ra = static_cast<uint8_t>((insn >> 16) & 0xfu);
        const uint8_t rb = static_cast<uint8_t>((insn >> 12) & 0xfu);
        const uint16_t uimm = static_cast<uint16_t>(insn & 0xffffu);
        const int32_t simm = static_cast<int32_t>(static_cast<int16_t>(uimm));

        uint32_t next_pc = pc + 1u;

        switch (op) {
            case op_halt:
                res.status = Status::halted;
                res.return_value = static_cast<int32_t>(regs_[rd]);
                res.final_pc = pc;
                res.remaining_fuel = fuel;
                return res;

            case op_movi:
                regs_[rd] = static_cast<uint32_t>(simm);
                break;

            case op_lui:
                regs_[rd] = static_cast<uint32_t>(uimm) << 16;
                break;

            case op_ori:
                regs_[rd] = regs_[ra] | static_cast<uint32_t>(uimm);
                break;

            case op_mov:
                regs_[rd] = regs_[ra];
                break;

            case op_add:
                regs_[rd] = regs_[ra] + regs_[rb];
                break;

            case op_sub:
                regs_[rd] = regs_[ra] - regs_[rb];
                break;

            case op_mul:
                regs_[rd] = regs_[ra] * regs_[rb];
                break;

            case op_div: {
                const int32_t lhs = static_cast<int32_t>(regs_[ra]);
                const int32_t rhs = static_cast<int32_t>(regs_[rb]);
                if (rhs == 0) {
                    res.status = Status::div_by_zero;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                if (lhs == INT32_MIN && rhs == -1) {
                    regs_[rd] = static_cast<uint32_t>(INT32_MIN);
                } else {
                    regs_[rd] = static_cast<uint32_t>(lhs / rhs);
                }
                break;
            }

            case op_and:
                regs_[rd] = regs_[ra] & regs_[rb];
                break;

            case op_or:
                regs_[rd] = regs_[ra] | regs_[rb];
                break;

            case op_xor:
                regs_[rd] = regs_[ra] ^ regs_[rb];
                break;

            case op_shl:
                regs_[rd] = regs_[ra] << (uimm & 31u);
                break;

            case op_shr:
                regs_[rd] = regs_[ra] >> (uimm & 31u);
                break;

            case op_addi:
                regs_[rd] = regs_[ra] + static_cast<uint32_t>(simm);
                break;

            case op_jmp:
                next_pc = uimm;
                break;

            case op_beq:
                if (regs_[rd] == regs_[ra]) next_pc = uimm;
                break;

            case op_bne:
                if (regs_[rd] != regs_[ra]) next_pc = uimm;
                break;

            case op_blt:
                if (static_cast<int32_t>(regs_[rd]) < static_cast<int32_t>(regs_[ra])) next_pc = uimm;
                break;

            case op_bge:
                if (static_cast<int32_t>(regs_[rd]) >= static_cast<int32_t>(regs_[ra])) next_pc = uimm;
                break;

            case op_load: {
                const uint32_t idx = regs_[ra] + static_cast<uint32_t>(simm);
                if (idx >= scratch_words) {
                    res.status = Status::mem_fault;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                regs_[rd] = scratch_[idx];
                break;
            }

            case op_store: {
                const uint32_t idx = regs_[ra] + static_cast<uint32_t>(simm);
                if (idx >= scratch_words) {
                    res.status = Status::mem_fault;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                scratch_[idx] = regs_[rd];
                break;
            }

            case op_sys: {
                const auto sid = static_cast<SyscallId>(uimm);
                if (sid == sys_poke && !policy.allow_poke) {
                    res.status = Status::syscall_denied;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                if (sid == sys_vault_write && !policy.allow_vault_write) {
                    res.status = Status::syscall_denied;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                if (!policy.on_syscall) {
                    res.status = Status::syscall_denied;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                uint32_t out_val = 0;
                if (!policy.on_syscall(sid, regs_[ra], regs_[rd], out_val, policy.context)) {
                    res.status = Status::syscall_denied;
                    res.final_pc = pc;
                    res.remaining_fuel = fuel;
                    return res;
                }
                regs_[rd] = out_val;
                break;
            }

            default:
                res.status = Status::illegal_opcode;
                res.final_pc = pc;
                res.remaining_fuel = fuel;
                return res;
        }
        pc = next_pc;
    }
}

} // namespace rtos::script
