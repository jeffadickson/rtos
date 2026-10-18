#pragma once
#include "rtos.hpp"
namespace rtos::detail {
// Bounded O(thread count) scheduling; no allocation and no intrusive user lists.
struct Scheduler {
    static constexpr unsigned capacity = RTOS_MAX_THREADS;
    Thread* threads[capacity]{};
    unsigned count{}, cursor{capacity - 1};
    Tick now{};
    bool contains(const Thread& t) const {
        for (unsigned i=0; i<count; ++i) if (threads[i] == &t) return true;
        return false;
    }
    bool overlaps(uintptr_t begin, size_t bytes) const {
        for (unsigned i=0; i<count; ++i) {
            const auto& t = *threads[i];
            if (begin < t.stack_begin+t.stack_bytes && t.stack_begin < begin+bytes) return true;
        }
        return false;
    }
    bool overlaps_control(uintptr_t begin, size_t bytes) const {
        for (unsigned i=0; i<count; ++i) {
            const auto control = reinterpret_cast<uintptr_t>(threads[i]);
            if (begin < control+sizeof(Thread) && control < begin+bytes) return true;
        }
        return false;
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
            if (t.state == State::sleeping && Tick(now - t.wake) < 0x80000000u)
                t.state = State::ready;
        }
    }
    Thread* pick(Thread* previous = nullptr, bool rotate = true) {
        if (!rotate && previous && previous->state == State::ready && contains(*previous)) {
            bool higher = false;
            for (unsigned i=0; i<count; ++i)
                if (threads[i]->state == State::ready && threads[i]->priority > previous->priority) higher = true;
            if (!higher) return previous;
        }
        Thread* best = nullptr;
        unsigned selected = cursor;
        for (unsigned n=1; n<=count; ++n) {
            unsigned i = (cursor + n) % count;
            auto* t = threads[i];
            if (t->state == State::ready && (!best || t->priority > best->priority)) {
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
