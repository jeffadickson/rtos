/** @file tests/tier1_flight.cpp
 * @brief Verifies Tier 1 Flight-Heritage Services:
 *   - BootHandoff ABI + HealthManager Golden/Flight A/B Safe-Mode fallback
 *   - RateMonotonicPeriod WCET cycle telemetry + deadline miss recovery
 *   - FixedPool<BlockBytes, Count> O(1) alloc/release, ISR path, double-free & bounds rejection
 *   - WorkQueue<MaxItems> immediate/delayed/ISR deferral + startup Barrier<Participants>
 *   - EpochClock dual-clock monotonic + step/slew + 32-bit tick rollover
 */
#include "sync_model.hpp"
#include "rtos_boot.hpp"
#include "rtos_clock.hpp"
#include "rtos_pool.hpp"
#include "rtos_rm.hpp"
#include "rtos_workqueue.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace rtos;

namespace {

void test_boot_handoff_and_health() {
    boot::BootHandoff h{};
    watchdog::CrashRecord crash{};

    // Cold power-on boot enters flight_a with attempt=1.
    boot::bootloader_prepare_handoff(h, boot::BootReason::power_on, 0x20260001u, 3u);
    assert(h.valid());
    assert(h.active_slot == static_cast<uint8_t>(boot::BootSlot::flight_a));
    assert(h.boot_attempts == 1u);
    assert(!h.safe_mode());

    // Tampering any byte invalidates CRC-32C immediately.
    h.image_build_id ^= 1u;
    assert(!h.valid());
    h.image_build_id ^= 1u;
    assert(h.valid());

    // Repeated watchdog resets trip Safe Mode and fall back to Golden slot on attempt 4 (> 3).
    boot::bootloader_prepare_handoff(h, boot::BootReason::watchdog, 0x20260001u, 3u);
    assert(h.boot_attempts == 2u && !h.safe_mode());
    boot::bootloader_prepare_handoff(h, boot::BootReason::cpu_fault, 0x20260001u, 3u);
    assert(h.boot_attempts == 3u && !h.safe_mode());
    boot::bootloader_prepare_handoff(h, boot::BootReason::watchdog, 0x20260001u, 3u);
    assert(h.boot_attempts == 4u);
    assert(h.safe_mode());
    assert(h.active_slot == static_cast<uint8_t>(boot::BootSlot::golden));

    // HealthManager records fatal crash & clears boot loop once confirmed healthy.
    boot::HealthManager hm;
    assert(hm.initialize(&h, &crash));
    assert(hm.in_safe_mode());
    assert(hm.active_slot() == boot::BootSlot::golden);

    hm.record_fatal(0xDEAD0001u, 0x80001234u, 2u, 900u, 850u);
    assert(crash.valid());
    assert(crash.fault_code == 0xDEAD0001u);
    assert(h.last_fault_pc == 0x80001234u);
    assert((h.flags & boot::kFlagCrashLogged) != 0u);

    assert(hm.confirm_boot_healthy());
    assert(h.boot_attempts == 0u);
    assert(h.confirmed_healthy());
}

void test_rate_monotonic_period() {
    RateMonotonicPeriod rm;
    Tick sleep_ticks = 0;

    // Invalid period rejected.
    assert(rm.step(0u, 100u, 1000u, sleep_ticks) == Result::invalid);

    // First call anchors deadline = 100 + 10 = 110.
    assert(rm.step(10u, 100u, 1000u, sleep_ticks) == Result::ok);
    assert(sleep_ticks == 0u);

    // Finish on time at tick 103 (exec = 450 cycles) -> sleep 7 ticks.
    assert(rm.step(10u, 103u, 1450u, sleep_ticks) == Result::ok);
    assert(sleep_ticks == 7u);

    // Finish on time at tick 114 (exec = 900 cycles) -> sleep 6 ticks.
    assert(rm.step(10u, 114u, 2350u, sleep_ticks) == Result::ok);
    assert(sleep_ticks == 6u);

    // Overrun: finish at tick 135 (missed deadline 130, exec = 3000 cycles) -> Result::timeout.
    assert(rm.step(10u, 135u, 5350u, sleep_ticks) == Result::timeout);
    assert(sleep_ticks == 0u);

    const auto st = rm.stats();
    assert(st.active);
    assert(st.count == 3u);
    assert(st.missed_deadlines == 1u);
    assert(st.min_exec_cycles == 450u);
    assert(st.max_exec_cycles == 3000u);
    assert(st.total_exec_cycles == 4350u);

    rm.cancel();
    rm.reset_stats();
    assert(!rm.stats().active && rm.stats().count == 0u);
}

static FixedPool<32, 4> test_pool;
static Thread worker_a, worker_b, worker_c;

void test_fixed_pool() {
    model::scheduler = {};
    model::isr = false;
    model::started = true;
    worker_a = {};
    model::use(worker_a, 2);
    assert(model::scheduler.add(worker_a));

    void* blocks[4]{};
    for (size_t i = 0; i < 4; ++i) {
        assert(test_pool.allocate(blocks[i], NoWait) == Result::ok);
        assert(blocks[i] != nullptr);
        std::memset(blocks[i], static_cast<int>(0xA0 + i), 32);
    }
    // Verify distinct non-overlapping blocks.
    for (size_t i = 0; i < 4; ++i) {
        for (size_t j = i + 1; j < 4; ++j) {
            assert(blocks[i] != blocks[j]);
        }
    }

    // Pool exhausted -> Result::empty.
    void* extra = nullptr;
    assert(test_pool.allocate(extra, NoWait) == Result::empty);
    assert(test_pool.snapshot().alloc_failures == 1u);
    assert(test_pool.snapshot().high_water == 4u);

    // Reject null, misaligned, and foreign pointers.
    uint8_t stack_buf[64]{};
    assert(test_pool.release(nullptr) == Result::invalid);
    assert(test_pool.release(stack_buf) == Result::invalid);
    assert(test_pool.release(static_cast<uint8_t*>(blocks[0]) + 1) == Result::invalid);

    // Release one block, then reject double-free!
    assert(test_pool.release(blocks[0]) == Result::ok);
    assert(test_pool.release(blocks[0]) == Result::invalid);
    assert(test_pool.snapshot().invalid_frees == 4u);

    // ISR allocate & release path.
    model::isr = true;
    void* isr_blk = nullptr;
    assert(test_pool.try_allocate_from_isr(isr_blk) == Result::ok);
    assert(isr_blk == blocks[0]);
    assert(test_pool.release_from_isr(isr_blk) == Result::ok);
    model::isr = false;

    for (size_t i = 1; i < 4; ++i) {
        assert(test_pool.release(blocks[i]) == Result::ok);
    }
    assert(test_pool.snapshot().free_blocks == 4u);
    assert(test_pool.snapshot().allocated == 0u);
}

static WorkQueue<4> workq;
static Barrier<3> startup_barrier;
static uint32_t work_sum = 0;

void work_add(void* ctx) {
    work_sum += *static_cast<uint32_t*>(ctx);
}

void test_workqueue_and_barrier() {
    model::scheduler = {};
    model::isr = false;
    model::started = true;
    worker_a = {}; worker_b = {}; worker_c = {};
    model::use(worker_c, 1);
    model::use(worker_b, 2);
    model::use(worker_a, 3);
    assert(model::scheduler.add(worker_a));
    assert(model::scheduler.add(worker_b));
    assert(model::scheduler.add(worker_c));

    uint32_t v1 = 10, v2 = 25, v3 = 100;
    assert(workq.submit(nullptr, nullptr) == Result::invalid);
    assert(workq.submit(work_add, &v1) == Result::ok);
    assert(workq.submit_delayed(work_add, &v2, 50u, 100u) == Result::ok);

    // Submit from ISR.
    model::isr = true;
    assert(workq.submit_from_isr(work_add, &v3) == Result::ok);
    model::isr = false;

    // Immediate & ISR items run at tick 110; delayed item stays pending until tick 150.
    assert(workq.poll_one(110u, NoWait) == Result::ok);
    assert(workq.poll_one(110u, NoWait) == Result::ok);
    assert(workq.poll_one(110u, NoWait) == Result::empty);
    assert(work_sum == 110u);

    // Delayed item executes at tick 150.
    assert(workq.poll_one(150u, NoWait) == Result::ok);
    assert(work_sum == 135u);
    assert(workq.snapshot().executed == 3u);
    assert(workq.snapshot().high_water == 3u);

    // Test Barrier<3>: first 2 participants park, 3rd releases all atomically.
    model::on_park = []() {
        // Simulate context switch to next ready thread during host test.
        const unsigned saved_depth = model::depth;
        model::depth = 0;
        if (model::caller == &worker_a) {
            model::caller = &worker_b;
            assert(startup_barrier.waiting_count() == 1u);
            assert(startup_barrier.wait(100u) == Result::ok);
        } else if (model::caller == &worker_b) {
            model::caller = &worker_c;
            assert(startup_barrier.waiting_count() == 2u);
            assert(startup_barrier.wait(100u) == Result::ok);
        }
        model::depth = saved_depth;
    };
    assert(startup_barrier.generation() == 0u);
    assert(startup_barrier.wait(100u) == Result::ok);
    model::on_park = nullptr;
    assert(startup_barrier.generation() == 1u);
    assert(startup_barrier.waiting_count() == 0u);
}

void test_epoch_clock() {
    EpochClock clk;
    assert(!clk.initialize(0u));
    assert(clk.initialize(1000u)); // 1 ms per tick

    assert(clk.monotonic_ns(10u) == 10000000ull);
    assert(!clk.synced());

    // Step epoch to 2026-01-01T00:00:00Z (~1,767,225,600 s) at tick 100.
    const uint64_t base_epoch = 1767225600ull * 1000000000ull;
    clk.set_epoch_ns(base_epoch, 100u);
    assert(clk.synced());
    assert(clk.epoch_ns(100u) == base_epoch);
    assert(clk.epoch_ns(1100u) == base_epoch + 1000000000ull);

    // Apply +1000 ppb (+1 us/s) slew and verify smooth adjustment over 10 seconds.
    clk.set_slew_ppb(1000, 1100u);
    assert(clk.slew_ppb() == 1000);
    const uint64_t after_10s = clk.epoch_ns(11100u);
    assert(after_10s == base_epoch + 11000000000ull + 10000ull);

    // Verify 32-bit tick rollover preserves monotonicity!
    const uint64_t pre_wrap = clk.monotonic_ns(0xFFFFFFF0u);
    const uint64_t post_wrap = clk.monotonic_ns(0x00000010u);
    assert(post_wrap > pre_wrap);
    assert(post_wrap - pre_wrap == 0x20ull * 1000000ull);
}

} // namespace

int main() {
    test_boot_handoff_and_health();
    test_rate_monotonic_period();
    test_fixed_pool();
    test_workqueue_and_barrier();
    test_epoch_clock();
    std::printf("PASS: Tier 1 flight services (BootHandoff, RateMonotonicPeriod, FixedPool, WorkQueue, Barrier, EpochClock).\n");
    return 0;
}
