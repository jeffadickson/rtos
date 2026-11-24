#pragma once
/**
 * @file rtos_topic.hpp
 * @brief Compile-time static typed Publish/Subscribe Topic bus (RTOS_TOPIC).
 *
 * Provides zero-heap, deterministic fan-out messaging between flight or bench
 * subsystems without dynamic topic registries or pointer ownership ambiguity.
 */

#include <stddef.h>
#include <stdint.h>

namespace rtos {

/// Fixed-capacity static Publish/Subscribe topic for trivially copyable telemetry or commands.
template <typename T, size_t MaxSubscribers>
struct Topic {
    static_assert(__is_trivially_copyable(T),
                  "Topic payload must be trivially copyable");
    static_assert(MaxSubscribers >= 1 && MaxSubscribers <= 32,
                  "Topic MaxSubscribers must be in 1..32");

    using Callback = void (*)(const T& sample, void* context);

    struct Subscriber {
        Callback callback{};
        void* context{};
        bool active{};
    };

    Subscriber subscribers[MaxSubscribers]{};
    T last_sample{};
    uint32_t publish_count{};
    bool has_sample{};

    /// Register a subscriber callback; returns false if callback is null or table is full.
    bool subscribe(Callback cb, void* context = nullptr, size_t* out_slot = nullptr) {
        if (!cb) return false;
        for (size_t i = 0; i < MaxSubscribers; ++i) {
            if (!subscribers[i].active) {
                subscribers[i].callback = cb;
                subscribers[i].context = context;
                subscribers[i].active = true;
                if (out_slot) *out_slot = i;
                return true;
            }
        }
        return false;
    }

    /// Remove a subscriber by slot index returned from subscribe().
    bool unsubscribe(size_t slot) {
        if (slot >= MaxSubscribers || !subscribers[slot].active) return false;
        subscribers[slot] = Subscriber{};
        return true;
    }

    /// Publish a sample synchronously to all active subscribers; returns count notified.
    size_t publish(const T& sample) {
        last_sample = sample;
        has_sample = true;
        ++publish_count;
        size_t notified = 0;
        for (size_t i = 0; i < MaxSubscribers; ++i) {
            if (subscribers[i].active && subscribers[i].callback) {
                subscribers[i].callback(sample, subscribers[i].context);
                ++notified;
            }
        }
        return notified;
    }

    /// Copy the most recently published sample, if any.
    bool latest(T& out) const {
        if (!has_sample) return false;
        out = last_sample;
        return true;
    }

    /// Number of active subscribers currently registered.
    size_t subscriber_count() const {
        size_t count = 0;
        for (size_t i = 0; i < MaxSubscribers; ++i) {
            if (subscribers[i].active) ++count;
        }
        return count;
    }

    /// Reset topic state and clear all subscribers.
    void reset() {
        for (size_t i = 0; i < MaxSubscribers; ++i) subscribers[i] = Subscriber{};
        last_sample = T{};
        publish_count = 0;
        has_sample = false;
    }
};

} // namespace rtos
