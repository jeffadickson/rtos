#pragma once
/** @file rtos.hpp
 * @brief Static single-hart thread API; see the Sphinx API and metrics chapters.
 * All functions below are main/application-thread APIs, never peripheral-ISR APIs.
 * Objects, stacks, arguments and borrowed names must outlive kernel execution.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_config.hpp"
#include "rtos_wait.hpp"

namespace rtos {
using Entry = void (*)(void*);
using Tick = uint32_t;
enum class State : uint8_t { unused, ready, sleeping, finished, blocked };
/** @brief Permanent thread control block, default-initialized before create().
 * Fields are kernel-owned after registration; use set_priority() and snapshot()
 * rather than editing or concurrently inspecting them. The running thread is
 * represented as ready. A finished thread keeps its reserved storage forever.
 */
struct Thread {
    /// Saved frame belongs to the port; entry/argument are the application call.
    uint32_t* context{};
    Entry entry{};
    void* argument{};
    Tick wake{};
    /// Base priority: larger numbers win. Effective priority includes donations.
    uint8_t priority{};
#if RTOS_SYNC
    uint8_t effective_priority{};
    detail::WaitNode wait{};
    /// Intrusive ownership/wait links are valid only under the scheduler lock.
    Mutex* owned_mutexes{};
    Mutex* waiting_mutex{};
    uint8_t held_mutexes{};
#endif
    State state{State::unused};
    const char* name{}; // Borrowed immutable string; must outlive the kernel.
    uintptr_t stack_begin{};
    size_t stack_bytes{};
#if RTOS_METRICS
    /// Timer residency and dispatch totals; includes attributed trap overhead.
    uint64_t runtime_cycles{};
    uint64_t dispatches{};
#endif
};
/** @brief Boot-owned, ABI-aligned stack; Bytes includes the initial saved frame.
 * The compile-time minimum is a layout check, not a proven application bound.
 * Budget call chains, formatter scratch and trap/C++ handler use separately.
 */
template<size_t Bytes> struct alignas(16) Stack {
    static_assert(Bytes >= 512 && Bytes % 16 == 0);
    uint32_t words[Bytes / 4]{};
};
/** @brief Register a thread at boot without allocating or running its entry.
 * @param priority Base priority in 0..255; larger values take precedence.
 * @param stack Permanent writable storage, aligned to 16 bytes.
 * @param bytes Storage extent: at least 512 bytes and a multiple of 16.
 * @param name Optional borrowed immutable diagnostic name.
 * @return false on a sealed/full registry, invalid input or storage overlap;
 * failure does not modify the candidate or registry. Actual pointer validity
 * and argument lifetime remain the caller's responsibility.
 */
bool create(Thread&, Entry, void*, uint8_t priority, void* stack, size_t bytes,
            const char* name = nullptr);
template<size_t N>
bool create(Thread& t, Entry e, void* arg, uint8_t p, Stack<N>& s, const char* name = nullptr) {
    return create(t, e, arg, p, s.words, N, name);
}
/** @brief Seal registration, install the timer/vector and dispatch; never returns.
 * clock_hz is the real machine-timer frequency, not a measured CPU-usage value.
 * Zero tick rate, clock below tick rate, repeated start or private-idle storage
 * conflict halts. Zero application threads is valid. No runtime constructors run.
 */
[[noreturn]] void start(uint32_t clock_hz, uint32_t tick_hz = RTOS_TICK_HZ);
/// Reconsider priorities and rotate equal-priority peers; a no-op before start.
void yield();
// Zero yields. Delays must be <= INT32_MAX ticks. Returns false for invalid delay.
bool sleep_for(Tick ticks);
/// Read the wrapping logical tick count under IRQ masking; zero before start.
Tick ticks();
/// Finish the caller permanently; owned mutexes become explicitly owner-dead.
[[noreturn]] void exit();
// Boot-registered threads only; yields immediately to reconsider priorities.
bool set_priority(Thread&, uint8_t priority);
struct ThreadInfo {
    /// Borrowed identities and names remain valid for the kernel lifetime.
    const Thread* id{};
    const char* name{};
    State state{};
    uint8_t priority{}, effective_priority{};
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
/** @brief Copy coherent diagnostics into caller-owned storage under IRQ masking.
 * Idle is the final row after start. Null/zero output returns the required count;
 * a short buffer is valid and reports written_threads separately. Names are
 * borrowed, not copied. Disabled metrics return zero counters, not fake usage.
 */
SystemInfo snapshot(ThreadInfo* output, size_t capacity);
}
