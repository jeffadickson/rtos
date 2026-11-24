#pragma once
/** @file rtos_timers.hpp
 * @brief Static fixed-capacity one-shot and periodic software timers (zero heap).
 *
 * Caller explicitly drives tick(now) from a dedicated timer/coordinator thread —
 * no hidden kernel worker thread and no dynamic allocation.
 */
#include "rtos.hpp"
#include <stddef.h>
#include <stdint.h>

namespace rtos {

using TimerCallback = void (*)(void*);

template<size_t MaxTimers>
class TimerTable {
    static_assert(MaxTimers >= 1 && MaxTimers <= 64, "MaxTimers must be 1..64");
    struct Slot {
        TimerCallback callback{};
        void* context{};
        Tick period{};
        Tick deadline{};
        bool periodic{};
        bool active{};
        bool allocated{};
    };
    Slot slots_[MaxTimers]{};

public:
    constexpr TimerTable() = default;
    TimerTable(const TimerTable&) = delete;
    TimerTable& operator=(const TimerTable&) = delete;

    /** @brief Reserve a permanent timer handle in 0..MaxTimers-1. */
    bool create(size_t& handle, TimerCallback callback, void* context, Tick period, bool periodic) {
        if (!callback || period == 0 || period > 0x7fffffffu) return false;
        for (size_t i = 0; i < MaxTimers; ++i) {
            if (!slots_[i].allocated) {
                slots_[i] = {callback, context, period, 0, periodic, false, true};
                handle = i;
                return true;
            }
        }
        return false;
    }

    bool start(size_t handle, Tick now) {
        if (handle >= MaxTimers || !slots_[handle].allocated) return false;
        slots_[handle].deadline = now + slots_[handle].period;
        slots_[handle].active = true;
        return true;
    }

    bool stop(size_t handle) {
        if (handle >= MaxTimers || !slots_[handle].allocated) return false;
        slots_[handle].active = false;
        return true;
    }

    bool active(size_t handle) const {
        return handle < MaxTimers && slots_[handle].allocated && slots_[handle].active;
    }

    /** @brief Fire all due timers at logical tick `now`; returns count fired. */
    size_t tick(Tick now) {
        size_t fired = 0;
        for (size_t i = 0; i < MaxTimers; ++i) {
            auto& s = slots_[i];
            if (!s.active) continue;
            if (static_cast<Tick>(now - s.deadline) < 0x80000000u) {
                if (s.periodic) {
                    s.deadline = now + s.period;
                } else {
                    s.active = false;
                }
                s.callback(s.context);
                ++fired;
            }
        }
        return fired;
    }

    /** @brief Minimum ticks until any active timer expires (0 if none or already due). */
    Tick next_delay(Tick now) const {
        bool any = false;
        Tick soonest = 0;
        for (size_t i = 0; i < MaxTimers; ++i) {
            if (!slots_[i].active) continue;
            const Tick rem = static_cast<Tick>(slots_[i].deadline - now);
            if (rem == 0 || rem >= 0x80000000u) return 0;
            if (!any || rem < soonest) {
                any = true;
                soonest = rem;
            }
        }
        return soonest;
    }
};

} // namespace rtos
