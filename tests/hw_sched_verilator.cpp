/** @file
 * @brief Verilator lockstep co-simulation of rtl/nexrt_hw_sched.sv against
 * rtos::hw_sched::Model + live TMR SEU fault-injection campaign.
 */
#include "Vnexrt_hw_sched.h"
#include "verilated.h"
#include "rtos_hw_sched.hpp"
#include <cassert>
#include <cstdio>
#include <cstdint>

using namespace rtos;

namespace {
class Harness {
public:
    Vnexrt_hw_sched dut;
    hw_sched::Model oracle;
    uint32_t rng{0xC0FFEE42u};

    uint32_t next_u32() {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void eval() { dut.eval(); }

    void reset() {
        dut.clk = 0;
        dut.rst_n = 0;
        dut.bus_wr = 0;
        dut.bus_rd = 0;
        dut.bus_addr = 0;
        dut.bus_wdata = 0;
        dut.periph_irq = 0;
        dut.fi_valid = 0;
        dut.fi_double = 0;
        dut.fi_slot = 0;
        dut.fi_mask = 0;
        eval();
        for (int i = 0; i < 4; ++i) {
            dut.clk = 1;
            eval();
            dut.clk = 0;
            eval();
        }
        dut.rst_n = 1;
        eval();
        oracle.reset();
    }

    uint32_t read_dut(uint32_t offset) {
        dut.bus_rd = 1;
        dut.bus_wr = 0;
        dut.bus_addr = offset & 0x3FFu;
        eval();
        const uint32_t v = dut.bus_rdata;
        dut.bus_rd = 0;
        eval();
        return v;
    }

    void write_both(uint32_t offset, uint32_t value, uint8_t periph_irq = 0) {
        dut.bus_wr = 1;
        dut.bus_rd = 0;
        dut.bus_addr = offset & 0x3FFu;
        dut.bus_wdata = value;
        dut.periph_irq = periph_irq;
        dut.clk = 1;
        eval();
        dut.clk = 0;
        dut.bus_wr = 0;
        dut.periph_irq = 0;
        eval();

        // Oracle executes step() then write() in the exact same clock-edge order as RTL.
        oracle.step(periph_irq);
        oracle.write(offset, value);
    }

    void tick_both(uint8_t periph_irq = 0, bool fi_valid = false, bool fi_double = false,
                   uint8_t fi_slot = 0, uint16_t fi_mask = 0) {
        dut.bus_wr = 0;
        dut.bus_rd = 0;
        dut.periph_irq = periph_irq;
        dut.fi_valid = fi_valid ? 1 : 0;
        dut.fi_double = fi_double ? 1 : 0;
        dut.fi_slot = fi_slot & 0xFu;
        dut.fi_mask = fi_mask;
        dut.clk = 1;
        eval();
        dut.clk = 0;
        dut.periph_irq = 0;
        dut.fi_valid = 0;
        dut.fi_double = 0;
        eval();

        oracle.step(periph_irq);
    }

    void check_lockstep(const char* context) {
        const uint32_t regs[] = {
            hw_sched::kRegCtrl, hw_sched::kRegStatus, hw_sched::kRegQuantum,
            hw_sched::kRegExpiredMask, hw_sched::kRegReadyMask, hw_sched::kRegFaultMask,
            hw_sched::kRegMtimeLo, hw_sched::kRegMtimeHi, hw_sched::kRegSliceRemain,
            hw_sched::kRegVersion
        };
        for (uint32_t r : regs) {
            const uint32_t got = read_dut(r);
            const uint32_t exp = oracle.read(r);
            if (got != exp) {
                std::fprintf(stderr, "LOCKSTEP MISMATCH (%s) reg=0x%03x dut=0x%08x oracle=0x%08x\n",
                             context, r, got, exp);
                assert(false);
            }
        }
        assert(static_cast<bool>(dut.sched_irq) == oracle.sched_irq());
        for (uint8_t tid = 0; tid < 8; ++tid) {
            const uint32_t fields[] = {
                hw_sched::kThrStatePrio, hw_sched::kThrRuntimeLo,
                hw_sched::kThrDispatches, hw_sched::kThrSliceUsed
            };
            for (uint32_t f : fields) {
                const uint32_t off = hw_sched::thread_offset(tid, f);
                const uint32_t got = read_dut(off);
                const uint32_t exp = oracle.read(off);
                if (got != exp) {
                    std::fprintf(stderr, "THREAD MISMATCH (%s) tid=%u field=0x%02x dut=0x%08x oracle=0x%08x\n",
                                 context, tid, f, got, exp);
                    assert(false);
                }
            }
        }
    }
};
} // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    bool trace_mode = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-' && argv[i][1] == '-' && argv[i][2] == 't' && argv[i][3] == 'r') {
            trace_mode = true;
        }
    }
    Harness h;
    h.reset();
    assert(h.read_dut(hw_sched::kRegVersion) == hw_sched::kVersion);

    auto emit_pick = [&]() {
        if (!trace_mode) return;
        std::printf("PICK");
        for (uint8_t t = 0; t < 16; ++t) {
            const uint32_t w = h.read_dut(hw_sched::thread_offset(t, hw_sched::kThrStatePrio));
            std::printf(" %u:%u", static_cast<unsigned>(hw_sched::unpack_state(w)),
                        static_cast<unsigned>(hw_sched::unpack_eff_prio(w)));
        }
        std::printf(" %u %u\n", static_cast<unsigned>(h.dut.next_valid_out),
                    static_cast<unsigned>(h.dut.next_tid_out));
    };

    // 1. Setup 8 threads with mixed priorities, deadlines, WCET budgets, and IRQ routes.
    for (uint8_t tid = 0; tid < 8; ++tid) {
        const uint8_t prio = static_cast<uint8_t>((tid < 2) ? 10 : (tid < 5) ? 5 : 2);
        const State st = (tid == 0) ? State::sleeping : State::ready;
        h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrStatePrio),
                     hw_sched::pack_state_prio(st, prio, prio));
        h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrWakeLo), 200u + tid * 50u);
        h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrWakeHi), 0u);
        h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrBudget), (tid == 7) ? 12u : 0u);
    }
    h.write_both(hw_sched::kRegIrqRoute0, hw_sched::kIrqRouteEnable | 0u);
    h.write_both(hw_sched::kRegQuantum, 10u);
    h.write_both(hw_sched::kRegCtrl, hw_sched::kCtrlEnable | hw_sched::kCtrlTickless |
                                     hw_sched::kCtrlRrEnable | hw_sched::kCtrlSealed | hw_sched::kCtrlIrqEn);
    emit_pick();
    h.write_both(hw_sched::kRegCommit, hw_sched::kCommitSwitch);
    h.check_lockstep("boot");

    // 2. 4,000-cycle randomized lockstep campaign (dynamic priorities, sleeps, IRQs, commits).
    for (unsigned step = 0; step < 4000; ++step) {
        const uint32_t r = h.next_u32();
        const uint8_t irq = ((r & 0xFFu) == 0x5Au) ? 0x01u : 0x00u;
        const uint32_t action = (r >> 8) % 10u;
        const uint8_t tid = static_cast<uint8_t>((r >> 16) & 7u);

        if (h.dut.sched_irq) {
            const uint32_t exp = h.read_dut(hw_sched::kRegExpiredMask);
            if (exp) h.write_both(hw_sched::kRegExpiredMask, exp);
            const uint32_t flt = h.read_dut(hw_sched::kRegFaultMask);
            if (flt) h.write_both(hw_sched::kRegFaultMask, flt);
            const auto st = hw_sched::decode_status(h.read_dut(hw_sched::kRegStatus));
            if (trace_mode && st.slice_expired && st.current_valid) {
                std::printf("ROTATE %u\n", static_cast<unsigned>(st.current_tid));
            }
            h.write_both(hw_sched::kRegCommit, hw_sched::kCommitSwitch |
                                               (st.slice_expired ? hw_sched::kCommitRotate : 0u));
            emit_pick();
        } else if (action == 0) {
            // Sleep a ready thread for 15..80 cycles
            const uint32_t cur_word = h.read_dut(hw_sched::thread_offset(tid, hw_sched::kThrStatePrio));
            if (hw_sched::unpack_state(cur_word) == State::ready) {
                const uint64_t now = h.read_dut(hw_sched::kRegMtimeLo);
                const uint64_t wake = now + 15u + (r & 63u);
                h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrWakeLo), static_cast<uint32_t>(wake));
                h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrStatePrio),
                             hw_sched::pack_state_prio(State::sleeping,
                                                       hw_sched::unpack_base_prio(cur_word),
                                                       hw_sched::unpack_eff_prio(cur_word)));
            }
        } else if (action == 1) {
            // Priority inheritance donation update
            const uint32_t cur_word = h.read_dut(hw_sched::thread_offset(tid, hw_sched::kThrStatePrio));
            const State st = hw_sched::unpack_state(cur_word);
            if (st != State::unused && st != State::quarantined) {
                const uint8_t bp = hw_sched::unpack_base_prio(cur_word);
                const uint8_t ep = static_cast<uint8_t>(bp + (r & 3u));
                h.write_both(hw_sched::thread_offset(tid, hw_sched::kThrStatePrio),
                             hw_sched::pack_state_prio(st, bp, ep));
            }
        } else {
            h.tick_both(irq);
        }

        if ((step % 16) == 0) {
            h.check_lockstep("randomized");
            emit_pick();
        }
    }
    h.check_lockstep("post-random");

    // 3. 1,000-SEU Single-Replica TMR Fault-Injection Campaign!
    unsigned seu_injected = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        const uint8_t slot = static_cast<uint8_t>(i & 7u);
        const uint16_t mask = static_cast<uint16_t>(1u << (i % 16u));
        h.tick_both(0, true, false, slot, mask);
        ++seu_injected;
        h.tick_both(0, false, false, 0, 0);
        assert(h.read_dut(hw_sched::kRegStatus) == h.oracle.read(hw_sched::kRegStatus));
        assert(h.read_dut(hw_sched::kRegReadyMask) == h.oracle.read(hw_sched::kRegReadyMask));
        if ((i % 20) == 0) emit_pick();
    }
    const uint32_t corrected = h.read_dut(hw_sched::kRegSeuCount);
    assert(corrected == seu_injected);
    assert(!h.dut.seu_fault_out);

    // 4. Negative Probe: dual-replica upset must flag sticky seu_fault_out!
    h.tick_both(0, true, true, 0, 0x0007u);
    assert(h.dut.seu_fault_out == 1);

    std::printf("HW_SCHED_VERILATOR_PASS lockstep_cycles=4000 seu_injected=%u seu_corrected=%u dual_fault_detected=1\n",
                seu_injected, corrected);
    return 0;
}
