#pragma once
#include "rtos.hpp"
#include "rtos_monitor.hpp"
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
    // Before the first pick, unsigned wrap makes cursor+1 select slot zero.
    // capacity-1 incorrectly rotates the first tie when count != capacity.
    unsigned count{}, cursor{static_cast<unsigned>(-1)};
    Tick now{};
    bool contains(const Thread& t) const {
        for (unsigned i=0; i<count; ++i) if (threads[i] == &t) return true;
        return false;
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
        threads[count++] = &t;
        return true;
    }
    void advance(Tick elapsed) {
        now += elapsed;
        for (unsigned i=0; i<count; ++i) {
            auto& t = *threads[i];
#if RTOS_SYNC
            expire_wait(t,now);
#endif
            if (t.state == State::sleeping && Tick(now - t.wake) < 0x80000000u) {
                t.state = State::ready;
                NEXRT_TRACE(ready,&t,nullptr,0);
            }
        }
    }
    Thread* pick(Thread* previous = nullptr, bool rotate = true) {
        if (!rotate && previous && previous->state == State::ready && contains(*previous)) {
            bool higher = false;
            for (unsigned i=0; i<count; ++i)
                if (threads[i]->state == State::ready && priority(*threads[i]) > priority(*previous)) higher = true;
            if (!higher) return previous;
        }
        Thread* best = nullptr;
        unsigned selected = cursor;
        for (unsigned n=1; n<=count; ++n) {
            unsigned i = (cursor + n) % count;
            auto* t = threads[i];
            if (t->state == State::ready && (!best || priority(*t) > priority(*best))) {
                best = t; selected = i;
            }
        }
        if (best) cursor = selected;
        return best;
    }
};
// No loop proportional to missed ticks. Hardware time is monotonic modulo 2^64.
inline uint64_t elapsed_ticks(uint64_t now, uint64_t deadline, uint32_t period) {
    return now < deadline ? 0 : (now-deadline)/period+1;
}
}
