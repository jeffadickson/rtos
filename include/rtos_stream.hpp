#pragma once
/** @file rtos_stream.hpp
 * @brief Static variable-length StreamBuffer and framed MessageBuffer (zero heap).
 *
 * Designed for UART RX/TX, variable-length telemetry frames, and flash/network
 * staging without allocating worst-case fixed Queue<MaxPacket, N> slots.
 */
#include "rtos_sync.hpp"
#include <stddef.h>
#include <stdint.h>

namespace rtos {

struct StreamInfo {
    size_t used{};
    size_t capacity{};
    size_t high_water{};
    uint32_t drops{};
};

/** @brief Static circular byte stream with scheduler-aware wait queues and ISR endpoints. */
template<size_t Capacity>
class StreamBuffer {
    static_assert(Capacity >= 8 && Capacity <= 65536, "StreamBuffer capacity must be 8..65536 bytes");
    uint8_t data_[Capacity]{};
    size_t head_{};
    size_t used_{};
    size_t high_water_{};
    uint32_t drops_{};
    detail::WaitQueue readers_;

    size_t write_locked(const uint8_t* src, size_t len, detail::Critical& lock) {
        const size_t free_bytes = Capacity - used_;
        const size_t n = len < free_bytes ? len : free_bytes;
        for (size_t i = 0; i < n; ++i) {
            data_[(head_ + used_ + i) % Capacity] = src[i];
        }
        used_ += n;
        if (used_ > high_water_) high_water_ = used_;
        if (n < len && drops_ != UINT32_MAX) ++drops_;
        if (n > 0) {
            if (auto* t = readers_.best()) {
                readers_.complete(*t, Result::ok);
                lock.changed();
            }
        }
        return n;
    }

    size_t read_locked(uint8_t* dst, size_t max_len) {
        const size_t n = max_len < used_ ? max_len : used_;
        for (size_t i = 0; i < n; ++i) {
            dst[i] = data_[(head_ + i) % Capacity];
        }
        head_ = (head_ + n) % Capacity;
        used_ -= n;
        return n;
    }

public:
    constexpr StreamBuffer() = default;
    StreamBuffer(const StreamBuffer&) = delete;
    StreamBuffer& operator=(const StreamBuffer&) = delete;

    /** @brief Copy up to len bytes into the stream from thread context; wakes waiting reader. */
    Result send(const void* bytes, size_t len, size_t& written) {
        written = 0;
        if (!bytes && len) return Result::invalid;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        if (len == 0) return Result::ok;
        written = write_locked(static_cast<const uint8_t*>(bytes), len, lock);
        return written > 0 ? Result::ok : Result::full;
    }

    /** @brief Non-blocking ISR byte producer; wakes waiting reader via deferred reschedule. */
    Result try_send_from_isr(const void* bytes, size_t len, size_t& written) {
        written = 0;
        if (!bytes && len) return Result::invalid;
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        if (len == 0) return Result::ok;
        written = write_locked(static_cast<const uint8_t*>(bytes), len, lock);
        return written > 0 ? Result::ok : Result::full;
    }

    /** @brief Read available bytes or block at most timeout ticks until data arrives. */
    Result receive(void* output, size_t capacity, size_t& read_bytes, Tick timeout = NoWait) {
        read_bytes = 0;
        if (!output && capacity) return Result::invalid;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        if (timeout > MaxWait) return Result::invalid;
        if (capacity == 0) return Result::ok;
        if (used_ == 0) {
            if (timeout == 0) return Result::empty;
            const auto r = readers_.wait(timeout);
            if (r != Result::ok) return r;
            if (used_ == 0) return Result::empty;
        }
        read_bytes = read_locked(static_cast<uint8_t*>(output), capacity);
        return read_bytes > 0 ? Result::ok : Result::empty;
    }

    /** @brief Non-blocking ISR consumer. */
    Result try_receive_from_isr(void* output, size_t capacity, size_t& read_bytes) {
        read_bytes = 0;
        if (!output && capacity) return Result::invalid;
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        if (used_ == 0) return Result::empty;
        read_bytes = read_locked(static_cast<uint8_t*>(output), capacity);
        return Result::ok;
    }

    StreamInfo snapshot() const {
        detail::Critical lock;
        return {used_, Capacity, high_water_, drops_};
    }
};

/** @brief Static variable-length atomic packet buffer (2-byte little-endian length prefix).
 * Either an entire message is stored/retrieved atomically or the call fails unchanged.
 */
template<size_t Capacity>
class MessageBuffer {
    static_assert(Capacity >= 16 && Capacity <= 65536, "MessageBuffer capacity must be 16..65536 bytes");
    uint8_t data_[Capacity]{};
    size_t head_{};
    size_t used_{};
    size_t high_water_{};
    uint32_t drops_{};
    detail::WaitQueue readers_;

    void push_byte(uint8_t b) {
        data_[(head_ + used_) % Capacity] = b;
        ++used_;
    }

    uint8_t pop_byte() {
        const uint8_t b = data_[head_];
        head_ = (head_ + 1) % Capacity;
        --used_;
        return b;
    }

    Result send_locked(const uint8_t* payload, size_t length, detail::Critical& lock) {
        if (length > 65535u || length + 2u > Capacity) return Result::invalid;
        if (Capacity - used_ < length + 2u) {
            if (drops_ != UINT32_MAX) ++drops_;
            return Result::full;
        }
        push_byte(static_cast<uint8_t>(length & 0xFFu));
        push_byte(static_cast<uint8_t>((length >> 8) & 0xFFu));
        for (size_t i = 0; i < length; ++i) push_byte(payload[i]);
        if (used_ > high_water_) high_water_ = used_;
        if (auto* t = readers_.best()) {
            readers_.complete(*t, Result::ok);
            lock.changed();
        }
        return Result::ok;
    }

    Result receive_locked(uint8_t* output, size_t capacity, size_t& out_length) {
        if (used_ < 2u) return Result::empty;
        const size_t msg_len = static_cast<size_t>(data_[head_]) |
                               (static_cast<size_t>(data_[(head_ + 1u) % Capacity]) << 8);
        if (msg_len > capacity) return Result::overflow;
        (void)pop_byte();
        (void)pop_byte();
        for (size_t i = 0; i < msg_len; ++i) output[i] = pop_byte();
        out_length = msg_len;
        return Result::ok;
    }

public:
    constexpr MessageBuffer() = default;
    MessageBuffer(const MessageBuffer&) = delete;
    MessageBuffer& operator=(const MessageBuffer&) = delete;

    Result send(const void* payload, size_t length) {
        if (!payload && length) return Result::invalid;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        return send_locked(static_cast<const uint8_t*>(payload), length, lock);
    }

    Result try_send_from_isr(const void* payload, size_t length) {
        if (!payload && length) return Result::invalid;
        detail::Critical lock;
        if (!detail::interrupt_context() || !detail::synchronization_running()) return Result::context;
        return send_locked(static_cast<const uint8_t*>(payload), length, lock);
    }

    Result receive(void* output, size_t capacity, size_t& out_length, Tick timeout = NoWait) {
        out_length = 0;
        if (!output && capacity) return Result::invalid;
        detail::Critical lock;
        if (!detail::calling_thread()) return Result::context;
        if (timeout > MaxWait) return Result::invalid;
        if (used_ < 2u) {
            if (timeout == 0) return Result::empty;
            const auto r = readers_.wait(timeout);
            if (r != Result::ok) return r;
        }
        return receive_locked(static_cast<uint8_t*>(output), capacity, out_length);
    }

    StreamInfo snapshot() const {
        detail::Critical lock;
        return {used_, Capacity, high_water_, drops_};
    }
};

} // namespace rtos
