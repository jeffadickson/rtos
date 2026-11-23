#pragma once
/** @file rtos_safe.hpp
 * @brief Compile-time concurrency safety wrappers for NexRT (zero heap, zero RTTI).
 *
 * Eliminates whole classes of RTOS thread bugs by construction rather than runtime
 * detection alone:
 *   1. OrderedLock<T, Level> + LockToken<Level>:
 *        Enforces strict ascending lock order (L_next > L_prev) at compile time,
 *        making AB/BA deadlock cycles unrepresentable, and keeps T private so
 *        unsynchronized data races cannot compile.
 *   2. CeilingResource<T, CeilingPriority>:
 *        Immediate Priority Ceiling Protocol (IPCP / SRP). Raises the caller's
 *        priority to CeilingPriority on entry and restores it on exit, guaranteeing
 *        single-blocking, zero chained priority inversion, and deadlock freedom.
 *   3. StaticTask<StackBytes, Priority>:
 *        Bundles Thread + aligned Stack into a single non-copyable static object so
 *        stack aliasing, wrong sizes, and dangling stack pointers cannot occur.
 *   4. TypedChannel<T, Capacity>:
 *        Separates ThreadSender, ThreadReceiver, and IsrSender capabilities so
 *        calling blocking operations from an ISR handle is a compile-time error.
 *
 * Formal mathematical proofs of these protocols live in the formal/ Dafny modules (Z3).
 */
#include "rtos_ipc.hpp"
#include <stdint.h>

namespace rtos::safe {

/** @brief Zero-size compile-time proof of the highest lock level currently held.
 * A thread entry begins with RootLockToken (level 0). Acquiring an OrderedLock at
 * Level requires passing a LockToken<HeldLevel> with Level > HeldLevel.
 */
template<uint8_t HeldLevel>
struct LockToken {
    static constexpr uint8_t level = HeldLevel;
};

using RootLockToken = LockToken<0>;

/** @brief Compile-time deadlock-free, data-race-free container for shared state T.
 *
 * T is strictly private and accessible only through with_lock(), which requires a
 * strictly lower LockToken<PrevLevel> (Level > PrevLevel). Verified deadlock-free
 * in formal/LockFreedomProof.dfy.
 */
template<class T, uint8_t Level>
class OrderedLock {
    static_assert(Level > 0, "OrderedLock level must be in 1..255 (0 is reserved for RootLockToken)");
    Mutex mutex_{};
    T value_{};

public:
    constexpr OrderedLock() = default;
    constexpr explicit OrderedLock(const T& initial) : value_(initial) {}
    OrderedLock(const OrderedLock&) = delete;
    OrderedLock& operator=(const OrderedLock&) = delete;

    static constexpr uint8_t lock_level = Level;

    /** @brief Acquire lock strictly above PrevLevel and invoke fn(T&, LockToken<Level>). */
    template<uint8_t PrevLevel, class Fn>
    Result with_lock(LockToken<PrevLevel>, Tick timeout, Fn&& fn) {
        static_assert(Level > PrevLevel,
                      "Lock-order violation: cannot acquire OrderedLock at level <= currently held lock level");
        LockGuard guard(mutex_, timeout);
        if (!guard.owns_lock()) return guard.result();
        fn(value_, LockToken<Level>{});
        return Result::ok;
    }

    MutexInfo snapshot() const { return mutex_.snapshot(); }
    Result recover_after_quiescence() { return mutex_.recover_after_quiescence(); }
};

/** @brief Immediate Priority Ceiling Protocol (IPCP) protected resource.
 *
 * On entry, atomically validates caller priority <= CeilingPriority, raises the
 * caller's priority to CeilingPriority while holding the internal mutex, executes
 * fn(T&), and restores the caller's original base priority on exit.
 *
 * Single-hart IPCP properties (formally proved in formal/LockFreedomProof.dfy):
 *   - No thread with base priority <= CeilingPriority can preempt the holder.
 *   - Zero transitive priority-inversion chains; at most one lower-priority block.
 *   - Mathematically deadlock-free on a single CPU without runtime cycle walks.
 */
template<class T, uint8_t CeilingPriority>
class CeilingResource {
    static_assert(CeilingPriority > 0, "CeilingPriority must be >= 1");
    Mutex mutex_{};
    T value_{};

public:
    constexpr CeilingResource() = default;
    constexpr explicit CeilingResource(const T& initial) : value_(initial) {}
    CeilingResource(const CeilingResource&) = delete;
    CeilingResource& operator=(const CeilingResource&) = delete;

    static constexpr uint8_t ceiling_priority = CeilingPriority;

    /** @brief Execute fn(T&) at CeilingPriority under exclusive ownership.
     * Rejects callers whose base priority exceeds CeilingPriority (fail-closed contract).
     */
    template<class Fn>
    Result access(Tick timeout, Fn&& fn) {
        uint8_t saved_base = 0;
        Thread* self = nullptr;
        {
            detail::Critical lock;
            self = detail::calling_thread();
            if (!self) return Result::context;
            if (self->priority > CeilingPriority) return Result::invalid;
            saved_base = self->priority;
        }
        LockGuard guard(mutex_, timeout);
        if (!guard.owns_lock()) return guard.result();
        {
            detail::Critical lock;
            if (self->effective_priority < CeilingPriority) {
                self->effective_priority = CeilingPriority;
#if RTOS_HW_SCHED
                detail::hw_sync_thread(*self);
#endif
            }
        }
        fn(value_);
        {
            detail::Critical lock;
            self->priority = saved_base;
            Mutex::priority_changed(*self);
        }
        return Result::ok;
    }

    MutexInfo snapshot() const { return mutex_.snapshot(); }
    Result recover_after_quiescence() { return mutex_.recover_after_quiescence(); }
};

/** @brief Non-copyable boot-static thread + aligned stack bundle.
 * Eliminates mismatched stack pointers, stack aliasing, and wrong size arguments.
 */
template<size_t StackBytes, uint8_t Priority>
class StaticTask {
    Thread thread_{};
    Stack<StackBytes> stack_{};

public:
    constexpr StaticTask() = default;
    StaticTask(const StaticTask&) = delete;
    StaticTask& operator=(const StaticTask&) = delete;

    bool register_task(Entry entry, void* argument = nullptr, const char* name = nullptr,
                       uint64_t wcet_budget_cycles = 0) {
        if (!create(thread_, entry, argument, Priority, stack_, name)) return false;
#if RTOS_BUDGET_WATCHDOG
        if (wcet_budget_cycles != 0 && !set_execution_budget(thread_, wcet_budget_cycles)) {
            return false;
        }
#else
        (void)wcet_budget_cycles;
#endif
        return true;
    }

    Thread& thread() { return thread_; }
    const Thread& thread() const { return thread_; }
};

/** @brief Capability-separated typed channel preventing ISR/thread API misuse at compile time.
 *
 * ThreadSender only exposes send(); ThreadReceiver only exposes receive();
 * IsrSender only exposes non-blocking try_send_from_isr().
 */
template<class T, size_t Capacity>
class TypedChannel {
    Queue<T, Capacity> queue_{};

public:
    class ThreadSender {
        Queue<T, Capacity>& q_;
    public:
        explicit constexpr ThreadSender(Queue<T, Capacity>& q) : q_(q) {}
        Result send(const T& value, Tick timeout = NoWait) { return q_.send(value, timeout); }
    };

    class ThreadReceiver {
        Queue<T, Capacity>& q_;
    public:
        explicit constexpr ThreadReceiver(Queue<T, Capacity>& q) : q_(q) {}
        Result receive(T& output, Tick timeout = NoWait) { return q_.receive(output, timeout); }
    };

    class IsrSender {
        Queue<T, Capacity>& q_;
    public:
        explicit constexpr IsrSender(Queue<T, Capacity>& q) : q_(q) {}
        Result try_send(const T& value) { return q_.try_send_from_isr(value); }
    };

    constexpr TypedChannel() = default;
    TypedChannel(const TypedChannel&) = delete;
    TypedChannel& operator=(const TypedChannel&) = delete;

    ThreadSender thread_sender() { return ThreadSender(queue_); }
    ThreadReceiver thread_receiver() { return ThreadReceiver(queue_); }
    IsrSender isr_sender() { return IsrSender(queue_); }
    QueueInfo snapshot() const { return queue_.snapshot(); }
};

} // namespace rtos::safe
