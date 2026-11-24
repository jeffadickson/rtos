#pragma once
/** @file rtos_pool.hpp
 * @brief RTEMS Partition / Zephyr k_mem_slab style deterministic O(1) fixed-size
 *        static block pool (RTOS_POOL, zero heap, zero fragmentation).
 *
 * Formally proved in formal/PoolProof.dfy for slot conservation, double-free
 * rejection, and zero aliasing.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_sync.hpp"

namespace rtos {

struct PoolStats {
    size_t block_bytes{};
    size_t capacity{};
    size_t allocated{};
    size_t free_blocks{};
    size_t high_water{};
    uint32_t alloc_failures{};
    uint32_t invalid_frees{};
};

template<size_t BlockBytes, size_t Count>
class FixedPool {
    static_assert(BlockBytes >= sizeof(uintptr_t) && BlockBytes <= 65536u,
                  "BlockBytes must be >= pointer size and <= 64 KiB");
    static_assert(Count >= 1u && Count <= 1024u, "Count must be 1..1024");
    static constexpr size_t kStride = (BlockBytes + 7u) & ~size_t(7u);

    alignas(8) uint8_t storage_[Count * kStride]{};
    bool in_use_[Count]{};
    uint16_t free_stack_[Count]{};
    size_t free_count_{Count};
    size_t high_water_{};
    uint32_t alloc_failures_{};
    uint32_t invalid_frees_{};
    bool initialized_{};
    detail::WaitQueue waiters_;

    void ensure_init() {
        if (initialized_) return;
        for (size_t i = 0; i < Count; ++i) {
            in_use_[i] = false;
            free_stack_[i] = static_cast<uint16_t>(Count - 1u - i);
        }
        free_count_ = Count;
        initialized_ = true;
    }

    void* pop_locked() {
        ensure_init();
        if (free_count_ == 0u) {
            if (alloc_failures_ != UINT32_MAX) ++alloc_failures_;
            return nullptr;
        }
        const uint16_t idx = free_stack_[--free_count_];
        in_use_[idx] = true;
        const size_t used = Count - free_count_;
        if (used > high_water_) high_water_ = used;
        return &storage_[size_t(idx) * kStride];
    }

    Result release_locked(void* ptr, detail::Critical& lock) {
        ensure_init();
        if (!ptr) {
            if (invalid_frees_ != UINT32_MAX) ++invalid_frees_;
            return Result::invalid;
        }
        const uintptr_t base = reinterpret_cast<uintptr_t>(storage_);
        const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t limit = base + sizeof(storage_);
        if (addr < base || addr >= limit || ((addr - base) % kStride) != 0u) {
            if (invalid_frees_ != UINT32_MAX) ++invalid_frees_;
            return Result::invalid;
        }
        const size_t idx = static_cast<size_t>((addr - base) / kStride);
        if (!in_use_[idx] || free_count_ >= Count) {
            if (invalid_frees_ != UINT32_MAX) ++invalid_frees_;
            return Result::invalid; // Reject double-free fail-closed!
        }
        in_use_[idx] = false;
        free_stack_[free_count_++] = static_cast<uint16_t>(idx);
        if (auto* t = waiters_.best()) {
            waiters_.complete(*t, Result::ok);
            lock.changed();
        }
        return Result::ok;
    }

public:
    constexpr FixedPool() = default;
    FixedPool(const FixedPool&) = delete;
    FixedPool& operator=(const FixedPool&) = delete;

    /// Allocate one fixed-size block from thread context; optionally blocks up to timeout ticks.
    Result allocate(void*& out_block, Tick timeout = NoWait) {
        out_block = nullptr;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        if (timeout > MaxWait) return Result::invalid;
        if (void* p = pop_locked()) {
            out_block = p;
            return Result::ok;
        }
        if (timeout == 0u) return Result::empty;
        const Result w = waiters_.wait(timeout);
        if (w != Result::ok) return w;
        if (void* p = pop_locked()) {
            out_block = p;
            return Result::ok;
        }
        return Result::empty;
    }

    /// Non-blocking ISR allocation.
    Result try_allocate_from_isr(void*& out_block) {
        out_block = nullptr;
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        if (void* p = pop_locked()) {
            out_block = p;
            return Result::ok;
        }
        return Result::empty;
    }

    /// Return a block from thread context; rejects null, misaligned, out-of-range, or double-free!
    Result release(void* ptr) {
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        return release_locked(ptr, lock);
    }

    /// Non-blocking ISR release; wakes waiting thread via deferred reschedule.
    Result release_from_isr(void* ptr) {
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        return release_locked(ptr, lock);
    }

    PoolStats snapshot() const {
        detail::Critical lock;
        const size_t free_n = initialized_ ? free_count_ : Count;
        return {BlockBytes, Count, Count - free_n, free_n, high_water_, alloc_failures_, invalid_frees_};
    }
};

} // namespace rtos
