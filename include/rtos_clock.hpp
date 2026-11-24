#pragma once
/** @file rtos_clock.hpp
 * @brief Dual-clock timebase (RTOS_CLOCK_EPOCH, zero heap): decouples monotonic
 *        scheduler ticks from a slewable/steppable TAI/UTC wall-clock epoch.
 *
 * Syncing time from 1PPS, GPS, or ground telecommands updates the epoch offset
 * without ever disrupting monotonic scheduler deadlines or timers.
 */
#include <stdint.h>
#include "rtos.hpp"

namespace rtos {

class EpochClock {
    uint64_t ns_per_tick_{1000000u}; // Default 1 ms/tick (1000 Hz)
    int64_t offset_ns_{};
    int32_t slew_ppb_{};             // Parts-per-billion frequency trim
    uint64_t anchor_ticks_{};
    uint32_t last_tick32_{};
    uint64_t tick64_high_{};
    bool synced_{};

    uint64_t extend_tick(Tick now_tick) {
        if (now_tick < last_tick32_) tick64_high_ += (uint64_t(1) << 32);
        last_tick32_ = now_tick;
        return tick64_high_ | now_tick;
    }

public:
    constexpr EpochClock() = default;

    bool initialize(uint32_t tick_hz) {
        if (tick_hz == 0u || tick_hz > 1000000000u) return false;
        ns_per_tick_ = 1000000000ull / tick_hz;
        offset_ns_ = 0;
        slew_ppb_ = 0;
        anchor_ticks_ = 0u;
        last_tick32_ = 0u;
        tick64_high_ = 0u;
        synced_ = false;
        return true;
    }

    /// Monotonic nanoseconds since boot (strictly non-decreasing, immune to epoch steps).
    uint64_t monotonic_ns(Tick now_tick) {
        return extend_tick(now_tick) * ns_per_tick_;
    }

    /// Step TAI/UTC epoch to exact nanoseconds since epoch (e.g., from ground TC / GPS 1PPS).
    void set_epoch_ns(uint64_t epoch_ns, Tick now_tick) {
        const uint64_t mono_ticks = extend_tick(now_tick);
        const uint64_t mono_ns = mono_ticks * ns_per_tick_;
        offset_ns_ = static_cast<int64_t>(epoch_ns) - static_cast<int64_t>(mono_ns);
        anchor_ticks_ = mono_ticks;
        synced_ = true;
    }

    /// Apply smooth frequency trim in parts-per-billion (clamped to +/- 500,000 ppb = +/- 500 ppm).
    void set_slew_ppb(int32_t ppb, Tick now_tick) {
        const uint64_t current = epoch_ns(now_tick);
        if (ppb > 500000) ppb = 500000;
        if (ppb < -500000) ppb = -500000;
        set_epoch_ns(current, now_tick);
        slew_ppb_ = ppb;
    }

    /// Current TAI/UTC epoch time in nanoseconds (incorporating monotonic progression + slew).
    uint64_t epoch_ns(Tick now_tick) {
        const uint64_t mono_ticks = extend_tick(now_tick);
        const uint64_t mono_ns = mono_ticks * ns_per_tick_;
        const int64_t delta_ns = static_cast<int64_t>((mono_ticks - anchor_ticks_) * ns_per_tick_);
        const int64_t slew_adj = (delta_ns / 1000000000ll) * static_cast<int64_t>(slew_ppb_);
        const int64_t result = static_cast<int64_t>(mono_ns) + offset_ns_ + slew_adj;
        return result > 0 ? static_cast<uint64_t>(result) : 0u;
    }

    bool synced() const { return synced_; }
    int32_t slew_ppb() const { return slew_ppb_; }
};

} // namespace rtos
