#pragma once
#include <stddef.h>
#include <stdint.h>
#include "rtos_config.hpp"

namespace rtos {
using Entry = void (*)(void*);
using Tick = uint32_t;
enum class State : uint8_t { unused, ready, sleeping, finished };
// Keep storage alive for the lifetime of the kernel. Larger priorities run first.
struct Thread {
    uint32_t* context{};
    Entry entry{};
    void* argument{};
    Tick wake{};
    uint8_t priority{};
    State state{State::unused};
    const char* name{}; // Borrowed immutable string; must outlive the kernel.
    uintptr_t stack_begin{};
    size_t stack_bytes{};
#if RTOS_METRICS
    uint64_t runtime_cycles{};
    uint64_t dispatches{};
#endif
};
template<size_t Bytes> struct alignas(16) Stack {
    static_assert(Bytes >= 512 && Bytes % 16 == 0);
    uint32_t words[Bytes / 4]{};
};
// Single hart, machine mode. Call create before start, from main only.
bool create(Thread&, Entry, void*, uint8_t priority, void* stack, size_t bytes,
            const char* name = nullptr);
template<size_t N>
bool create(Thread& t, Entry e, void* arg, uint8_t p, Stack<N>& s, const char* name = nullptr) {
    return create(t, e, arg, p, s.words, N, name);
}
[[noreturn]] void start(uint32_t clock_hz, uint32_t tick_hz = RTOS_TICK_HZ);
void yield();
// Zero yields. Delays must be <= INT32_MAX ticks. Returns false for invalid delay.
bool sleep_for(Tick ticks);
Tick ticks();
[[noreturn]] void exit();
// Boot-registered threads only; yields immediately to reconsider priorities.
bool set_priority(Thread&, uint8_t priority);
struct ThreadInfo {
    const Thread* id{};
    const char* name{};
    State state{};
    uint8_t priority{};
    bool current{}, idle{}, guard_ok{};
    size_t stack_bytes{};
    uint64_t runtime_cycles{}, dispatches{};
};
struct SystemInfo {
    Tick ticks{};
    uint32_t clock_hz{}, timer_period_cycles{};
    uint64_t elapsed_cycles{};
    size_t required_threads{}, written_threads{};
    bool metrics_enabled{};
};
// Coherent bounded copy into caller storage, including idle as the final row.
// With null/zero storage returns system info and the required row count.
SystemInfo snapshot(ThreadInfo* output, size_t capacity);
}
