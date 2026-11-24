#pragma once
#include "rtos.hpp"
#if RTOS_SYNC
namespace rtos::detail { void expire_wait(Thread&,Tick); }
#endif
namespace rtos::detail {
// Compare half-open storage ranges without forming a potentially overflowing end.
inline bool ranges_overlap(uintptr_t a, size_t a_bytes, uintptr_t b, size_t b_bytes) {
    return a_bytes && b_bytes && (a <= b ? b-a < a_bytes : a-b < b_bytes);
}
// Bounded O(thread count) scheduling; no allocation and no intrusive user lists.
struct Scheduler {
    static constexpr unsigned capacity = RTOS_MAX_THREADS;
    static uint8_t priority(const Thread& t) {
#if RTOS_SYNC
        return t.effective_priority;
#else
        return t.priority;
#endif
    }
    Thread* threads[capacity]{};
    uint8_t order[capacity]{};
    unsigned count{};
    Tick now{};
    bool contains(const Thread& t) const {
        for (unsigned i=0; i<count; ++i) if (threads[i] == &t) return true;
        return false;
    }
    int index_of(const Thread& t) const {
        for (unsigned i=0; i<count; ++i) if (threads[i] == &t) return static_cast<int>(i);
        return -1;
    }
    bool overlaps(uintptr_t begin, size_t bytes) const {
        for (unsigned i=0; i<count; ++i) {
            const auto& t = *threads[i];
            if (ranges_overlap(begin,bytes,t.stack_begin,t.stack_bytes)) return true;
        }
        return false;
    }
    bool overlaps_control(uintptr_t begin, size_t bytes) const {
        for (unsigned i=0; i<count; ++i) {
            const auto control = reinterpret_cast<uintptr_t>(threads[i]);
            if (ranges_overlap(begin,bytes,control,sizeof(Thread))) return true;
        }
        return false;
    }
    bool conflicts(uintptr_t begin, size_t bytes) const {
        return overlaps(begin,bytes) || overlaps_control(begin,bytes);
    }
    bool add(Thread& t) {
        if (count == capacity) return false;
        for (unsigned i=0; i<count; ++i) if (threads[i] == &t) return false;
        order[count] = static_cast<uint8_t>(count);
        threads[count++] = &t;
        return true;
    }
    void move_to_back(uint8_t slot) {
        if constexpr (capacity <= 1) {
            (void)slot;
            return;
        } else {
            const unsigned limit = count < capacity ? count : capacity;
            if (limit <= 1) return;
            unsigned pos = 0;
            while (pos < limit && order[pos] != slot) ++pos;
            if (pos >= limit) return;
            for (unsigned i = pos; i + 1 < limit; ++i) order[i] = order[i + 1];
            order[limit - 1] = slot;
        }
    }
    void advance(Tick elapsed) {
        now += elapsed;
        for (unsigned i=0; i<count; ++i) {
            auto& t = *threads[i];
#if RTOS_SYNC
            expire_wait(t,now);
            if (t.state == State::blocked && t.waiting_notify && Tick(now - t.wake) < 0x80000000u) {
                t.waiting_notify = false;
                t.wait.result = detail::WaitResult::timeout;
                t.state = State::ready;
            }
#endif
            if (t.state == State::sleeping && Tick(now - t.wake) < 0x80000000u)
                t.state = State::ready;
        }
    }
    /// Minimum remaining ticks until the next sleeping/blocked deadline (0 if any thread is ready, due, or none exist).
    Tick next_wake_delay() const {
        bool any = false;
        Tick soonest = 0;
        for (unsigned i = 0; i < count; ++i) {
            const auto& t = *threads[i];
            if (t.state == State::ready) return 0;
            if (t.state == State::sleeping || t.state == State::blocked) {
                const Tick remaining = static_cast<Tick>(t.wake - now);
                if (remaining >= 0x80000000u || remaining == 0) return 0;
                if (!any || remaining < soonest) {
                    any = true;
                    soonest = remaining;
                }
            }
        }
        return soonest;
    }
    Thread* pick(Thread* previous = nullptr, bool rotate = true) {
        if (!rotate && previous && previous->state == State::ready && contains(*previous)) {
            bool higher = false;
            for (unsigned i=0; i<count; ++i) {
                if (threads[i]->state == State::ready && priority(*threads[i]) > priority(*previous)) {
                    higher = true;
                    break;
                }
            }
            if (!higher) return previous;
        }
        if (rotate && previous) {
            const int prev_slot = index_of(*previous);
            if (prev_slot >= 0) move_to_back(static_cast<uint8_t>(prev_slot));
        }
        Thread* best = nullptr;
        uint8_t best_slot = 0;
        for (unsigned i=0; i<count; ++i) {
            const uint8_t slot = order[i];
            auto* t = threads[slot];
            if (t->state == State::ready && (!best || priority(*t) > priority(*best))) {
                best = t;
                best_slot = slot;
            }
        }
        if (rotate && !previous && best) move_to_back(best_slot);
        return best;
    }
};
// No loop proportional to missed ticks. Hardware time is monotonic modulo 2^64.
inline uint64_t elapsed_ticks(uint64_t now, uint64_t deadline, uint32_t period) {
    return now < deadline ? 0 : (now-deadline)/period+1;
}
}
