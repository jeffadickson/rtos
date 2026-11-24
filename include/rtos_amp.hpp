#pragma once
/**
 * @file rtos_amp.hpp
 * @brief Zero-heap lock-free SPSC shared-memory AMP mailbox channel (RTOS_AMP).
 *
 * Designed for asymmetric multiprocessing (e.g., ZynqMP Cortex-A53 <-> Cortex-R5F
 * or dual-hart RISCue) using freestanding acquire/release compiler intrinsics
 * (no libstdc++ <atomic> or libatomic RMW requirement on RV32IM without A ext),
 * cache-line separated indices, and an optional hardware doorbell/IPI callback.
 */

#include <stddef.h>
#include <stdint.h>

namespace rtos::amp {

/// Optional hardware doorbell / inter-processor interrupt callback.
using DoorbellFn = void (*)(void* context);

/// Lock-free single-producer / single-consumer shared-memory ring channel.
template <typename T, size_t Capacity>
struct AmpChannel {
    static_assert(__is_trivially_copyable(T),
                  "AmpChannel message type must be trivially copyable");
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1u)) == 0,
                  "AmpChannel Capacity must be a power of two >= 2");

    static constexpr uint32_t mask = static_cast<uint32_t>(Capacity - 1u);

    alignas(32) uint32_t head{0};    // Owned exclusively by producer
    alignas(32) uint32_t tail{0};    // Owned exclusively by consumer
    uint32_t dropped{0};             // Owned exclusively by producer
    T slots[Capacity]{};

    /// Reset indices (call only while both cores are quiesced during boot/init).
    void reset() {
        __atomic_store_n(&head, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&tail, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&dropped, 0u, __ATOMIC_RELAXED);
    }

    /// Producer: enqueue one message; returns false if full and increments dropped counter.
    bool send(const T& msg, DoorbellFn doorbell = nullptr, void* doorbell_ctx = nullptr) {
        const uint32_t h = __atomic_load_n(&head, __ATOMIC_RELAXED);
        const uint32_t t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
        if (static_cast<uint32_t>(h - t) >= static_cast<uint32_t>(Capacity)) {
            const uint32_t d = __atomic_load_n(&dropped, __ATOMIC_RELAXED);
            __atomic_store_n(&dropped, d + 1u, __ATOMIC_RELAXED);
            return false;
        }
        slots[h & mask] = msg;
        __atomic_store_n(&head, h + 1u, __ATOMIC_RELEASE);
        if (doorbell) doorbell(doorbell_ctx);
        return true;
    }

    /// Consumer: dequeue one message; returns false if empty.
    bool receive(T& out) {
        const uint32_t t = __atomic_load_n(&tail, __ATOMIC_RELAXED);
        const uint32_t h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
        if (h == t) return false;
        out = slots[t & mask];
        __atomic_store_n(&tail, t + 1u, __ATOMIC_RELEASE);
        return true;
    }

    /// Number of messages currently queued for the consumer.
    uint32_t available() const {
        const uint32_t h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
        const uint32_t t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
        return static_cast<uint32_t>(h - t);
    }

    /// Number of free message slots remaining for the producer.
    uint32_t free_slots() const {
        const uint32_t used = available();
        return used >= Capacity ? 0u : static_cast<uint32_t>(Capacity) - used;
    }

    bool is_empty() const { return available() == 0; }
    bool is_full() const { return available() >= Capacity; }
    uint32_t dropped_count() const { return __atomic_load_n(&dropped, __ATOMIC_RELAXED); }
};

} // namespace rtos::amp
