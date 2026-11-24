#pragma once
/** @file rtos_rm.hpp
 * @brief RTEMS-style Rate Monotonic Period manager (RTOS_RM_PERIOD, zero heap).
 *
 * Supervises periodic real-time control loops: tracks total periods, deadline
 * misses, and observed min/max/avg execution cycles (live WCET telemetry).
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos.hpp"
#include "rtos_sync.hpp"

namespace rtos {

struct RateMonotonicStats {
    uint32_t count{};
    uint32_t missed_deadlines{};
    uint64_t min_exec_cycles{};
    uint64_t max_exec_cycles{};
    uint64_t total_exec_cycles{};
    Tick period_ticks{};
    bool active{};
};

/** @brief Deterministic periodic loop supervisor inspired by RTEMS Rate Monotonic Manager. */
class RateMonotonicPeriod {
    Tick period_{};
    Tick next_deadline_{};
    uint64_t work_start_cycles_{};
    uint32_t count_{};
    uint32_t missed_{};
    uint64_t min_cycles_{};
    uint64_t max_cycles_{};
    uint64_t total_cycles_{};
    bool active_{};

public:
    constexpr RateMonotonicPeriod() = default;
    RateMonotonicPeriod(const RateMonotonicPeriod&) = delete;
    RateMonotonicPeriod& operator=(const RateMonotonicPeriod&) = delete;

    /// Start or complete one period using explicit caller-provided tick/cycle readings (host & target testable).
    Result step(Tick period_ticks, Tick now_tick, uint64_t now_cycles, Tick& out_sleep_ticks) {
        out_sleep_ticks = 0u;
        if (period_ticks == 0u || period_ticks > 0x7fffffffu) return Result::invalid;

        if (!active_) {
            period_ = period_ticks;
            next_deadline_ = now_tick + period_ticks;
            work_start_cycles_ = now_cycles;
            active_ = true;
            return Result::ok;
        }

        // Measure execution cycles spent during the just-finished iteration.
        const uint64_t exec = now_cycles >= work_start_cycles_ ? (now_cycles - work_start_cycles_) : 0u;
        if (count_ == 0u) {
            min_cycles_ = max_cycles_ = exec;
        } else {
            if (exec < min_cycles_) min_cycles_ = exec;
            if (exec > max_cycles_) max_cycles_ = exec;
        }
        total_cycles_ += exec;
        ++count_;

        period_ = period_ticks;
        // Wrap-safe deadline check: if now_tick >= next_deadline_, deadline was missed!
        const Tick lateness = static_cast<Tick>(now_tick - next_deadline_);
        if (lateness < 0x80000000u) {
            ++missed_;
            // Re-anchor cleanly from now_tick to prevent cascading backlog storms.
            next_deadline_ = now_tick + period_ticks;
            work_start_cycles_ = now_cycles;
            out_sleep_ticks = 0u;
            return Result::timeout;
        }

        out_sleep_ticks = static_cast<Tick>(next_deadline_ - now_tick);
        next_deadline_ = next_deadline_ + period_ticks;
        work_start_cycles_ = now_cycles;
        return Result::ok;
    }

    /// Convenience firmware loop call: sleeps until the next period boundary and records misses.
    Result wait(Tick period_ticks, uint64_t now_cycles = 0u) {
        Tick delay = 0u;
        const Result r = step(period_ticks, ticks(), now_cycles, delay);
        if (r == Result::ok && delay > 0u) {
            (void)sleep_for(delay);
        }
        return r;
    }

    void cancel() { active_ = false; }

    void reset_stats() {
        count_ = 0u;
        missed_ = 0u;
        min_cycles_ = 0u;
        max_cycles_ = 0u;
        total_cycles_ = 0u;
    }

    RateMonotonicStats stats() const {
        return {count_, missed_, min_cycles_, max_cycles_, total_cycles_, period_, active_};
    }
};

} // namespace rtos
