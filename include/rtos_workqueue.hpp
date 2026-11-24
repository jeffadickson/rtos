#pragma once
/** @file rtos_workqueue.hpp
 * @brief Zephyr/FreeRTOS-style static WorkQueue (ISR deferral onto one shared
 *        worker stack) and RTEMS-style startup Barrier (RTOS_WORKQUEUE, zero heap).
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_sync.hpp"

namespace rtos {

using WorkHandler = void (*)(void*);

struct WorkItem {
    WorkHandler handler{};
    void* context{};
    Tick ready_tick{};
    bool delayed{};
};

struct WorkQueueStats {
    size_t pending{};
    size_t capacity{};
    size_t high_water{};
    uint32_t executed{};
    uint32_t drops{};
};

/** @brief Boot-static workqueue with ISR submission and optional delayed execution. */
template<size_t MaxItems>
class WorkQueue {
    static_assert(MaxItems >= 1u && MaxItems <= 128u, "MaxItems must be 1..128");
    WorkItem items_[MaxItems]{};
    bool occupied_[MaxItems]{};
    size_t count_{};
    size_t high_water_{};
    uint32_t executed_{};
    uint32_t drops_{};
    detail::WaitQueue workers_;

    Result enqueue_locked(const WorkItem& item, detail::Critical& lock) {
        if (!item.handler) return Result::invalid;
        for (size_t i = 0; i < MaxItems; ++i) {
            if (!occupied_[i]) {
                items_[i] = item;
                occupied_[i] = true;
                ++count_;
                if (count_ > high_water_) high_water_ = count_;
                if (auto* t = workers_.best()) {
                    workers_.complete(*t, Result::ok);
                    lock.changed();
                }
                return Result::ok;
            }
        }
        if (drops_ != UINT32_MAX) ++drops_;
        return Result::full;
    }

public:
    constexpr WorkQueue() = default;
    WorkQueue(const WorkQueue&) = delete;
    WorkQueue& operator=(const WorkQueue&) = delete;

    Result submit(WorkHandler handler, void* context = nullptr) {
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        return enqueue_locked({handler, context, 0u, false}, lock);
    }

    Result submit_delayed(WorkHandler handler, void* context, Tick delay_ticks, Tick now) {
        if (delay_ticks > 0x7fffffffu) return Result::invalid;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        return enqueue_locked({handler, context, now + delay_ticks, delay_ticks > 0u}, lock);
    }

    /// FreeRTOS xTimerPendFunctionCallFromISR / Zephyr k_work_submit equivalent (non-blocking).
    Result submit_from_isr(WorkHandler handler, void* context = nullptr) {
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        return enqueue_locked({handler, context, 0u, false}, lock);
    }

    /// Pop and execute at most one ready work item at logical tick `now`.
    Result poll_one(Tick now, Tick timeout = NoWait) {
        WorkItem next{};
        bool found = false;
        {
            detail::Critical lock;
            if (!detail::calling_thread()) return Result::context;
            if (timeout > MaxWait) return Result::invalid;

            auto find_ready = [&]() -> bool {
                for (size_t i = 0; i < MaxItems; ++i) {
                    if (!occupied_[i]) continue;
                    if (!items_[i].delayed || static_cast<Tick>(now - items_[i].ready_tick) < 0x80000000u) {
                        next = items_[i];
                        occupied_[i] = false;
                        --count_;
                        return true;
                    }
                }
                return false;
            };

            found = find_ready();
            if (!found) {
                if (timeout == 0u) return Result::empty;
                const Result w = workers_.wait(timeout);
                if (w != Result::ok) return w;
                found = find_ready();
            }
        }
        if (!found) return Result::empty;
        // Execute handler outside the scheduler lock!
        next.handler(next.context);
        {
            detail::Critical lock;
            ++executed_;
        }
        return Result::ok;
    }

    WorkQueueStats snapshot() const {
        detail::Critical lock;
        return {count_, MaxItems, high_water_, executed_, drops_};
    }
};

/** @brief RTEMS-style deterministic multi-task startup Barrier (zero heap).
 * Releases all waiting threads atomically once `Participants` threads arrive.
 */
template<size_t Participants>
class Barrier {
    static_assert(Participants >= 1u && Participants <= 64u, "Participants must be 1..64");
    size_t arrived_{};
    uint32_t generation_{};
    detail::WaitQueue waiters_;

public:
    constexpr Barrier() = default;
    Barrier(const Barrier&) = delete;
    Barrier& operator=(const Barrier&) = delete;

    Result wait(Tick timeout = MaxWait) {
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        if (timeout > MaxWait) return Result::invalid;
        if (arrived_ + 1u >= Participants) {
            arrived_ = 0u;
            ++generation_;
            while (auto* t = waiters_.best()) {
                waiters_.complete(*t, Result::ok);
                lock.changed();
            }
            return Result::ok;
        }
        ++arrived_;
        const uint32_t my_gen = generation_;
        const Result r = waiters_.wait(timeout);
        if (r != Result::ok && generation_ == my_gen && arrived_ > 0u) {
            --arrived_;
        }
        return r;
    }

    uint32_t generation() const {
        detail::Critical lock;
        return generation_;
    }

    size_t waiting_count() const {
        detail::Critical lock;
        return arrived_;
    }
};

} // namespace rtos
