/** @file tests/script_vm.cpp
 * @brief Verifies the zero-heap fuel-bounded diagnostic bytecode VM (RTOS_SCRIPT)
 *        AND runs end-to-end bytecode compiled by the C# NexRT.Script compiler.
 */
#include "rtos_script.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace rtos::script;

namespace {

struct MockBoard {
    uint32_t sensor_readings[5]{3300, 3050, 3250, 3000, 3400};
    size_t sensor_idx{0};
    uint32_t low_logs{0};
    uint32_t board_id_logged{0};
    uint32_t emitted_ch{0};
    uint32_t emitted_val{0};
    uint32_t poke_count{0};
    uint32_t vault_val{777};
};

bool board_syscall(SyscallId id, uint32_t arg0, uint32_t arg1, uint32_t& out_val, void* ctx) {
    auto* b = static_cast<MockBoard*>(ctx);
    switch (id) {
        case sys_sensor:
            if (arg0 != 0) return false;
            out_val = b->sensor_readings[b->sensor_idx % 5u];
            ++b->sensor_idx;
            return true;
        case sys_peek:
            // Explicit MMIO allowlist: only 0x40001000 is readable!
            if (arg0 != 0x40001000u) return false;
            out_val = 0xCAFEu;
            return true;
        case sys_poke:
            if (arg0 != 0x40001004u) return false;
            ++b->poke_count;
            out_val = arg1;
            return true;
        case sys_sleep:
            out_val = 0;
            return arg0 <= 1000u;
        case sys_log:
            if (arg0 == 1u) ++b->low_logs;
            if (arg0 == 99u) b->board_id_logged = arg1;
            out_val = 0;
            return true;
        case sys_vault_read:
            out_val = b->vault_val;
            return true;
        case sys_vault_write:
            b->vault_val = arg1;
            out_val = arg1;
            return true;
        case sys_emit:
            b->emitted_ch = arg0;
            b->emitted_val = arg1;
            out_val = arg1;
            return true;
    }
    return false;
}

void test_arithmetic_and_safety() {
    uint8_t pkg[512]{};
    Vm vm;
    MockBoard board;
    HostPolicy pol{};
    pol.max_fuel_cap = 256;
    pol.on_syscall = board_syscall;
    pol.context = &board;

    // 1. Basic arithmetic + scratchpad store/load + halt.
    const uint32_t prog1[] = {
        encode_imm(op_movi, 1, 0, 40),
        encode_imm(op_movi, 2, 0, 2),
        encode_reg(op_add, 3, 1, 2),
        encode_imm(op_store, 3, 0, 7),  // scratch[7] = 42
        encode_imm(op_load, 4, 0, 7),   // r4 = scratch[7]
        encode_imm(op_halt, 4, 0, 0),
    };
    const size_t sz1 = build_package(prog1, 6, 100, 0, pkg, sizeof(pkg));
    assert(sz1 == 20u + 24u);
    auto r1 = vm.execute(pkg, sz1, pol);
    assert(r1.status == Status::halted && r1.return_value == 42);
    assert(vm.scratch(7) == 42u);

    // 2. CRC-32C tamper detection rejects single-bit flip before execution!
    pkg[22] ^= 0x01u;
    auto r_bad = vm.execute(pkg, sz1, pol);
    assert(r_bad.status == Status::bad_crc && r_bad.instructions_executed == 0u);

    // 3. Infinite loop exhausts fuel deterministically.
    const uint32_t prog_loop[] = {
        encode_imm(op_jmp, 0, 0, 0),
    };
    const size_t sz_loop = build_package(prog_loop, 1, 50, 0, pkg, sizeof(pkg));
    auto r_loop = vm.execute(pkg, sz_loop, pol);
    assert(r_loop.status == Status::fuel_exhausted);
    assert(r_loop.instructions_executed == 50u);

    // 4. Out-of-bounds scratchpad store fails closed.
    const uint32_t prog_oob[] = {
        encode_imm(op_store, 1, 0, 64),
    };
    const size_t sz_oob = build_package(prog_oob, 1, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_oob, pol).status == Status::mem_fault);

    // 5. Divide by zero fails closed.
    const uint32_t prog_div0[] = {
        encode_imm(op_movi, 1, 0, 10),
        encode_imm(op_movi, 2, 0, 0),
        encode_reg(op_div, 3, 1, 2),
    };
    const size_t sz_div0 = build_package(prog_div0, 3, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_div0, pol).status == Status::div_by_zero);

    // 6. Illegal opcode & PC out-of-bounds fail closed.
    const uint32_t prog_ill[] = { encode_imm(0xFEu, 0, 0, 0) };
    const size_t sz_ill = build_package(prog_ill, 1, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_ill, pol).status == Status::illegal_opcode);

    const uint32_t prog_pc[] = { encode_imm(op_jmp, 0, 0, 99) };
    const size_t sz_pc = build_package(prog_pc, 1, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_pc, pol).status == Status::pc_out_of_bounds);

    // 7. Capability enforcement: poke denied when allow_poke=false, allowed when true!
    const uint32_t prog_poke[] = {
        encode_imm(op_lui, 1, 0, static_cast<int16_t>(0x4000u)),
        encode_imm(op_ori, 1, 1, 0x1004),
        encode_imm(op_movi, 2, 0, 123),
        encode_imm(op_sys, 2, 1, sys_poke),
        encode_imm(op_halt, 2, 0, 0),
    };
    const size_t sz_poke = build_package(prog_poke, 5, 100, 0, pkg, sizeof(pkg));
    pol.allow_poke = false;
    assert(vm.execute(pkg, sz_poke, pol).status == Status::syscall_denied);
    assert(board.poke_count == 0u);

    pol.allow_poke = true;
    auto r_poke = vm.execute(pkg, sz_poke, pol);
    assert(r_poke.status == Status::halted && r_poke.return_value == 123);
    assert(board.poke_count == 1u);

    // 8. Full ALU, bitwise, shift, branch (BEQ/BNE/BLT/BGE), and INT32_MIN / -1 saturation coverage.
    const uint32_t prog_alu[] = {
        encode_imm(op_movi, 1, 0, 100),                  // 0: r1 = 100
        encode_imm(op_movi, 2, 0, 35),                   // 1: r2 = 35
        encode_reg(op_sub, 3, 1, 2),                     // 2: r3 = 65
        encode_imm(op_movi, 4, 0, 3),                    // 3: r4 = 3
        encode_reg(op_mul, 5, 3, 4),                     // 4: r5 = 195
        encode_imm(op_movi, 6, 0, 0x0F),                 // 5: r6 = 0x0F
        encode_reg(op_and, 7, 5, 6),                     // 6: r7 = 195 & 15 = 3
        encode_imm(op_movi, 8, 0, 0x30),                 // 7: r8 = 0x30
        encode_reg(op_or, 9, 7, 8),                      // 8: r9 = 0x33
        encode_reg(op_xor, 10, 9, 6),                    // 9: r10 = 0x3C (60)
        encode_imm(op_shl, 11, 10, 2),                   // 10: r11 = 240
        encode_imm(op_shr, 12, 11, 3),                   // 11: r12 = 30
        encode_imm(op_mov, 13, 12, 0),                   // 12: r13 = 30
        encode_imm(op_beq, 12, 13, 15),                  // 13: taken -> 15
        encode_imm(op_halt, 0, 0, 0),                    // 14: skipped
        encode_imm(op_bne, 12, 1, 17),                   // 15: 30 != 100 -> 17
        encode_imm(op_halt, 0, 0, 0),                    // 16: skipped
        encode_imm(op_bge, 1, 12, 19),                   // 17: 100 >= 30 -> 19
        encode_imm(op_halt, 0, 0, 0),                    // 18: skipped
        encode_imm(op_lui, 14, 0, static_cast<int16_t>(0x8000u)), // 19: r14 = INT32_MIN
        encode_imm(op_movi, 15, 0, -1),                  // 20: r15 = -1
        encode_reg(op_div, 14, 14, 15),                  // 21: INT32_MIN / -1 -> INT32_MIN (no UB!)
        encode_imm(op_addi, 12, 12, 12),                 // 22: r12 = 42
        encode_imm(op_halt, 12, 0, 0),                   // 23: halt(42)
    };
    const size_t sz_alu = build_package(prog_alu, 24, 100, 0, pkg, sizeof(pkg));
    auto r_alu = vm.execute(pkg, sz_alu, pol);
    assert(r_alu.status == Status::halted && r_alu.return_value == 42);
    assert(vm.reg(14) == 0x80000000u);

    // 9. Vault write capability & flag_allow_vault_write / flag_allow_poke header checks + null syscall hook.
    const uint32_t prog_vw[] = {
        encode_imm(op_movi, 1, 0, 0),
        encode_imm(op_movi, 2, 0, 999),
        encode_imm(op_sys, 2, 1, sys_vault_write),
        encode_imm(op_halt, 2, 0, 0),
    };
    const size_t sz_vw_flagged = build_package(prog_vw, 4, 50, flag_allow_vault_write, pkg, sizeof(pkg));
    pol.allow_vault_write = false;
    assert(vm.execute(pkg, sz_vw_flagged, pol).status == Status::syscall_denied);
    const size_t sz_vw_unflagged = build_package(prog_vw, 4, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_vw_unflagged, pol).status == Status::syscall_denied);
    pol.allow_vault_write = true;
    auto r_vw = vm.execute(pkg, sz_vw_flagged, pol);
    assert(r_vw.status == Status::halted && r_vw.return_value == 999 && board.vault_val == 999u);

    // Header flag_allow_poke denied before running even 1 instruction.
    const size_t sz_pf = build_package(prog1, 6, 50, flag_allow_poke, pkg, sizeof(pkg));
    pol.allow_poke = false;
    auto r_pf = vm.execute(pkg, sz_pf, pol);
    assert(r_pf.status == Status::syscall_denied && r_pf.instructions_executed == 0u);

    // Null syscall handler & out-of-bounds load fail closed.
    HostPolicy no_sys = pol;
    no_sys.on_syscall = nullptr;
    const size_t sz_vw_nosys = build_package(prog_vw, 4, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_vw_nosys, no_sys).status == Status::syscall_denied);

    const uint32_t prog_oob_ld[] = { encode_imm(op_load, 1, 0, 64) };
    const size_t sz_oob_ld = build_package(prog_oob_ld, 1, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_oob_ld, pol).status == Status::mem_fault);

    // 10. Malformed packages & build_package boundary checks.
    assert(build_package(nullptr, 1, 10, 0, pkg, sizeof(pkg)) == 0u);
    assert(build_package(prog1, 0, 10, 0, pkg, sizeof(pkg)) == 0u);
    assert(build_package(prog1, 6, 10, 0, pkg, 10u) == 0u);
    assert(vm.execute(nullptr, 0, pol).status == Status::invalid_package);
    assert(vm.execute(pkg, 10u, pol).status == Status::invalid_package);
    const size_t sz_ok = build_package(prog1, 6, 50, 0, pkg, sizeof(pkg));
    assert(vm.execute(pkg, sz_ok - 4u, pol).status == Status::invalid_package); // wrong byte len
    pkg[0] = 0u; // bad magic
    assert(vm.execute(pkg, sz_ok, pol).status == Status::invalid_package);
    (void)build_package(prog1, 6, 50, 0, pkg, sizeof(pkg));
    pkg[8] = 0u; pkg[9] = 0u; pkg[10] = 0u; pkg[11] = 0u; // code_words == 0
    assert(vm.execute(pkg, sz_ok, pol).status == Status::invalid_package);
    assert(crc32c(nullptr, 10u) == 0u);
}

void test_compiled_nxs_file(const char* path) {
    FILE* f = std::fopen(path, "rb");
    assert(f != nullptr);
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    assert(n > 20);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> pkg(static_cast<size_t>(n));
    assert(std::fread(pkg.data(), 1, pkg.size(), f) == pkg.size());
    std::fclose(f);

    MockBoard board;
    HostPolicy pol{};
    pol.max_fuel_cap = 1024;
    pol.on_syscall = board_syscall;
    pol.context = &board;

    Vm vm;
    const RunResult r = vm.execute(pkg.data(), pkg.size(), pol);
    // Expected average of {3300, 3050, 3250, 3000, 3400} = 16000 / 5 = 3200!
    // Low readings (< 3100): 3050 and 3000 -> low_logs == 2!
    assert(r.status == Status::halted);
    assert(r.return_value == 3200);
    assert(board.board_id_logged == 0xCAFEu);
    assert(board.low_logs == 2u);
    assert(board.emitted_ch == 10u && board.emitted_val == 3200u);
    std::printf("PASS: Executed C#-compiled .nxs bytecode (%zu bytes, %u insns, ret=%d, low_logs=%u).\n",
                pkg.size(), r.instructions_executed, r.return_value, board.low_logs);
}

} // namespace

int main(int argc, char** argv) {
    test_arithmetic_and_safety();
    if (argc >= 2) {
        test_compiled_nxs_file(argv[1]);
    }
    std::printf("PASS: Sandboxed diagnostic bytecode VM (RTOS_SCRIPT) verified.\n");
    return 0;
}
