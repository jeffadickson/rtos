/** @file
 * @brief Verifies Phase 1-3 invariants on the host:
 *   1. Fair per-priority round-robin without high-priority preemption starvation.
 *   2. Non-rotating priority reconsideration when RTOS_ROUND_ROBIN == 0.
 *   3. Cycle-accurate NexRT-HWSched C++ reference oracle (tickless deadlines,
 *      1-cycle peripheral IRQ wakeups, sealed registry, WCET budget quarantine).
 */
#include "scheduler.hpp"
#include "rtos_hw_sched.hpp"
#include <cassert>
#include <cstdio>

using namespace rtos;

static void set_prio(Thread& t, uint8_t value) {
    t.priority = value;
#if RTOS_SYNC
    t.effective_priority = value;
#endif
}

static void test_starvation_free_round_robin() {
    detail::Scheduler s;
    Thread high{}, low_a{}, low_b{};
    high.state = State::sleeping;
    low_a.state = State::ready;
    low_b.state = State::ready;
    set_prio(high, 5);
    set_prio(low_a, 1);
    set_prio(low_b, 1);
    assert(s.add(high) && s.add(low_a) && s.add(low_b));

    // Classic single-cursor bug scenario: high wakes periodically while low_a/low_b alternate.
    unsigned count_a = 0;
    unsigned count_b = 0;
    Thread* current = nullptr;
    for (unsigned tick = 0; tick < 20; ++tick) {
        if ((tick % 2) == 0) {
            high.state = State::sleeping;
            current = s.pick(current, true);
            if (current == &low_a) ++count_a;
            if (current == &low_b) ++count_b;
        } else {
            high.state = State::ready;
            current = s.pick(current, true);
            assert(current == &high);
        }
    }
    assert(count_a == 5 && count_b == 5);

    // Non-rotating priority reconsideration must never rotate equal-priority peers.
    high.state = State::sleeping;
    low_a.state = State::ready;
    low_b.state = State::ready;
    assert(s.pick(&low_a, false) == &low_a);
    assert(s.pick(&low_b, false) == &low_b);
    // Higher priority still preempts even with rotate=false.
    high.state = State::ready;
    assert(s.pick(&low_a, false) == &high);
}

static void test_hw_sched_oracle() {
    hw_sched::Model m;
    assert(m.read(hw_sched::kRegVersion) == hw_sched::kVersion);

    // Configure 3 threads: tid 0 = high (prio 5, sleeping), tid 1 = low_a (prio 2), tid 2 = low_b (prio 2).
    m.write(hw_sched::thread_offset(0, hw_sched::kThrStatePrio),
            hw_sched::pack_state_prio(State::sleeping, 5, 5));
    m.write(hw_sched::thread_offset(0, hw_sched::kThrWakeLo), 15u);
    m.write(hw_sched::thread_offset(0, hw_sched::kThrWakeHi), 0u);
    m.write(hw_sched::thread_offset(1, hw_sched::kThrStatePrio),
            hw_sched::pack_state_prio(State::ready, 2, 2));
    m.write(hw_sched::thread_offset(2, hw_sched::kThrStatePrio),
            hw_sched::pack_state_prio(State::ready, 2, 2));
    m.write(hw_sched::thread_offset(2, hw_sched::kThrBudget), 4u); // WCET watchdog = 4 cycles
    m.write(hw_sched::kRegIrqRoute0, hw_sched::kIrqRouteEnable | 0u); // IRQ0 wakes tid 0
    m.write(hw_sched::kRegQuantum, 5u);
    m.write(hw_sched::kRegCtrl, hw_sched::kCtrlEnable | hw_sched::kCtrlTickless |
                                hw_sched::kCtrlRrEnable | hw_sched::kCtrlSealed | hw_sched::kCtrlIrqEn);

    // Sealed registry rejects post-boot slot registration.
    m.write(hw_sched::thread_offset(3, hw_sched::kThrStatePrio),
            hw_sched::pack_state_prio(State::ready, 9, 9));
    assert(hw_sched::unpack_state(m.read(hw_sched::thread_offset(3, hw_sched::kThrStatePrio))) == State::unused);

    // Commit initial thread -> tid 1 runs.
    m.write(hw_sched::kRegCommit, hw_sched::kCommitSwitch);
    auto st = hw_sched::decode_status(m.read(hw_sched::kRegStatus));
    assert(st.current_valid && st.current_tid == 1);
    assert(!m.sched_irq());

    // Step 5 cycles -> quantum expires -> preempt_req asserts for RR rotation to tid 2.
    for (int i = 0; i < 4; ++i) {
        m.step();
        assert(!m.sched_irq());
    }
    m.step();
    assert(m.sched_irq());
    assert(m.read(hw_sched::thread_offset(1, hw_sched::kThrRuntimeLo)) == 5u);

    // Rotate & commit -> tid 2 runs.
    m.write(hw_sched::kRegCommit, hw_sched::kCommitSwitch | hw_sched::kCommitRotate);
    st = hw_sched::decode_status(m.read(hw_sched::kRegStatus));
    assert(st.current_valid && st.current_tid == 2);

    // tid 2 has WCET budget = 4 cycles -> 5th continuous cycle quarantines tid 2!
    for (int i = 0; i < 4; ++i) m.step();
    assert(m.read(hw_sched::kRegFaultMask) == 0u);
    m.step(); // cycle 10: slice_used == 5 > 4 -> quarantined!
    assert((m.read(hw_sched::kRegFaultMask) & (1u << 2)) != 0u);
    assert(hw_sched::unpack_state(m.read(hw_sched::thread_offset(2, hw_sched::kThrStatePrio))) == State::quarantined);
    assert(m.sched_irq());
    m.write(hw_sched::kRegFaultMask, 1u << 2);
    m.write(hw_sched::kRegCommit, hw_sched::kCommitSwitch);
    st = hw_sched::decode_status(m.read(hw_sched::kRegStatus));
    assert(st.current_valid && st.current_tid == 1);

    // Tickless mode: tid 1 is now sole ready peer at prio 2 -> quantum expires without spurious IRQ!
    for (int i = 0; i < 4; ++i) {
        m.step(); // mtime = 11, 12, 13, 14
        assert(!m.sched_irq());
    }
    // Cycle 15: tid 0's hardware deadline comparator fires automatically!
    m.step(); // mtime = 15 == wake[0]
    assert((m.read(hw_sched::kRegExpiredMask) & (1u << 0)) != 0u);
    assert(m.sched_irq());
    st = hw_sched::decode_status(m.read(hw_sched::kRegStatus));
    assert(st.next_valid && st.next_tid == 0);
    m.write(hw_sched::kRegExpiredMask, 1u << 0);
    m.write(hw_sched::kRegCommit, hw_sched::kCommitSwitch);
    assert(hw_sched::decode_status(m.read(hw_sched::kRegStatus)).current_tid == 0);

    // Put tid 0 to sleep with far deadline, then verify 1-cycle peripheral IRQ wakeup!
    m.write(hw_sched::thread_offset(0, hw_sched::kThrWakeLo), 100000u);
    m.write(hw_sched::thread_offset(0, hw_sched::kThrStatePrio),
            hw_sched::pack_state_prio(State::sleeping, 5, 5));
    m.write(hw_sched::kRegCommit, hw_sched::kCommitSwitch);
    assert(hw_sched::decode_status(m.read(hw_sched::kRegStatus)).current_tid == 1);
    m.step(0x01u); // Assert periph_irq[0] -> 1-cycle wake of tid 0!
    assert(m.sched_irq());
    assert(hw_sched::decode_status(m.read(hw_sched::kRegStatus)).next_tid == 0);
}

int main() {
    test_starvation_free_round_robin();
    test_hw_sched_oracle();
    std::printf("HW_SCHED_MODEL_PASS\n");
    return 0;
}
