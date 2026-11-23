/** @file
 * @brief Verilator verification of Phase 4 RISCue-MT banked hardware register file:
 *   - 1-cycle context bank switching across all 8 banks with zero leakage
 *   - x0 hardwired to zero across all banks
 *   - Banked mepc / mstatus isolation
 *   - spill_required assertion for tid >= NUM_BANKS or idle
 *   - 500-SEU single-replica TMR fault injection & scrubbing + dual-replica fault detection
 */
#include "Vriscue_banked_rf.h"
#include "verilated.h"
#include <cassert>
#include <cstdio>
#include <cstdint>

namespace {
void step(Vriscue_banked_rf& dut) {
    dut.clk = 1;
    dut.eval();
    dut.clk = 0;
    dut.eval();
}
} // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vriscue_banked_rf dut;
    dut.clk = 0;
    dut.rst_n = 0;
    dut.active_tid = 0;
    dut.active_valid = 0;
    dut.rs1_addr = 0;
    dut.rs2_addr = 0;
    dut.rd_we = 0;
    dut.rd_addr = 0;
    dut.rd_wdata = 0;
    dut.mepc_we = 0;
    dut.mepc_wdata = 0;
    dut.mstatus_we = 0;
    dut.mstatus_wdata = 0;
    dut.fi_valid = 0;
    dut.fi_double = 0;
    dut.fi_bank = 0;
    dut.fi_reg = 0;
    dut.fi_mask = 0;
    for (int i = 0; i < 4; ++i) step(dut);
    dut.rst_n = 1;
    dut.eval();

    // Idle (active_valid == 0) must assert spill_required.
    assert(dut.spill_required == 1);

    // Populate unique canary values in every register (x0..x31) + mepc/mstatus of all 8 banks.
    dut.active_valid = 1;
    for (uint8_t bank = 0; bank < 8; ++bank) {
        dut.active_tid = bank;
        dut.eval();
        assert(dut.spill_required == 0);
        for (uint8_t reg = 0; reg < 32; ++reg) {
            dut.rd_we = 1;
            dut.rd_addr = reg;
            dut.rd_wdata = 0xA5000000u | (static_cast<uint32_t>(bank) << 16) | reg;
            step(dut);
        }
        dut.rd_we = 0;
        dut.mepc_we = 1;
        dut.mepc_wdata = 0x00001000u + static_cast<uint32_t>(bank) * 0x100u;
        dut.mstatus_we = 1;
        dut.mstatus_wdata = 0x00001880u | bank;
        step(dut);
        dut.mepc_we = 0;
        dut.mstatus_we = 0;
    }

    // Verify spill_required for threads 8..15.
    for (uint8_t tid = 8; tid < 16; ++tid) {
        dut.active_tid = tid;
        dut.eval();
        assert(dut.spill_required == 1);
    }

    // Verify 1-cycle combinational bank switch across all 8 banks (zero clock latency!).
    for (uint8_t bank = 0; bank < 8; ++bank) {
        dut.active_tid = bank;
        dut.rs1_addr = 0;
        dut.rs2_addr = 31;
        dut.eval();
        assert(dut.rs1_data == 0u); // x0 hardwired to zero
        assert(dut.rs2_data == (0xA5000000u | (static_cast<uint32_t>(bank) << 16) | 31u));
        assert(dut.mepc_rdata == 0x00001000u + static_cast<uint32_t>(bank) * 0x100u);
        assert(dut.mstatus_rdata == (0x00001880u | bank));
        for (uint8_t reg = 1; reg < 32; ++reg) {
            dut.rs1_addr = reg;
            dut.eval();
            assert(dut.rs1_data == (0xA5000000u | (static_cast<uint32_t>(bank) << 16) | reg));
        }
    }

    // 500-SEU TMR fault-injection campaign across all 8 banks x 31 registers.
    for (unsigned i = 0; i < 500; ++i) {
        const uint8_t bank = static_cast<uint8_t>(i & 7u);
        const uint8_t reg = static_cast<uint8_t>(1u + (i % 31u));
        dut.active_tid = bank;
        dut.fi_valid = 1;
        dut.fi_double = 0;
        dut.fi_bank = bank;
        dut.fi_reg = reg;
        dut.fi_mask = 1u << (i % 32u);
        step(dut);
        dut.fi_valid = 0;
        // Immediately read corrupted register: TMR voter masks the SEU in 0 extra cycles!
        dut.rs1_addr = reg;
        dut.eval();
        assert(dut.rs1_data == (0xA5000000u | (static_cast<uint32_t>(bank) << 16) | reg));
        // Run 31 cycles so the background scrubber finishes a full pass and repairs the replica.
        for (int s = 0; s < 31; ++s) step(dut);
    }
    assert(dut.seu_count >= 500u);
    assert(dut.seu_fault == 0);

    // Negative probe: dual-replica upset triggers sticky seu_fault.
    dut.fi_valid = 1;
    dut.fi_double = 1;
    dut.fi_bank = 3;
    dut.fi_reg = 7;
    dut.fi_mask = 0x00000001u;
    step(dut);
    dut.fi_valid = 0;
    dut.fi_double = 0;
    dut.eval();
    assert(dut.seu_fault == 1);

    std::printf("BANKED_RF_VERILATOR_PASS banks=8 seu_scrubbed=%u dual_fault_detected=1\n",
                dut.seu_count);
    return 0;
}
