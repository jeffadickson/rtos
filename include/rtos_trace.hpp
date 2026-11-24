#pragma once
/**
 * @file rtos_trace.hpp
 * @brief Zero-heap binary flight recorder ring buffer (RTOS_TRACE).
 *
 * Captures fixed 16-byte binary events in O(1) time with wrap-around retention
 * and deterministic serialization for JournalFs crash/flight dumps or monitor inspection.
 */

#include <stddef.h>
#include <stdint.h>

namespace rtos::trace {

/// Standard NexRT binary trace event identifiers (0x0100+ reserved for application events).
enum EventId : uint16_t {
    ev_none             = 0x0000u,
    ev_boot             = 0x0001u,
    ev_dispatch         = 0x0002u,
    ev_sleep            = 0x0003u,
    ev_irq_enter        = 0x0004u,
    ev_irq_exit         = 0x0005u,
    ev_deadline_miss    = 0x0006u,
    ev_watchdog_fault   = 0x0007u,
    ev_storage_ecc      = 0x0008u,
    ev_storage_badblock = 0x0009u,
    ev_script_run       = 0x000Au,
    ev_elf_load         = 0x000Bu,
    ev_user_base        = 0x0100u,
};

/// Fixed 16-byte binary flight-recorder event record.
struct Event {
    uint64_t timestamp{};
    uint16_t event_id{};
    uint16_t source_id{};
    uint32_t arg{};
};

static_assert(sizeof(Event) == 16, "trace::Event must be exactly 16 bytes");

/// Serialized binary dump header (16 bytes, little-endian).
struct DumpHeader {
    uint32_t magic{};          ///< 0x5254584E ("NXTR")
    uint32_t version{};        ///< 1
    uint32_t retained_count{}; ///< Number of Event entries following this header
    uint32_t total_recorded{}; ///< Total events recorded since clear() (wraps modulo 2^32)
};

static_assert(sizeof(DumpHeader) == 16, "trace::DumpHeader must be exactly 16 bytes");

constexpr uint32_t dump_magic = 0x5254584Eu;
constexpr uint32_t dump_version = 1u;

/// Power-of-two overwriting flight recorder ring buffer.
template <size_t Capacity>
struct TraceRing {
    static_assert(Capacity >= 8 && (Capacity & (Capacity - 1u)) == 0,
                  "TraceRing Capacity must be a power of two >= 8");

    Event entries[Capacity]{};
    uint32_t total_recorded{};
    uint32_t overwritten{};

    /// Record a single binary event in O(1) time.
    void record(uint64_t timestamp, uint16_t event_id, uint16_t source_id, uint32_t arg) {
        const size_t slot = static_cast<size_t>(total_recorded) & (Capacity - 1u);
        entries[slot].timestamp = timestamp;
        entries[slot].event_id = event_id;
        entries[slot].source_id = source_id;
        entries[slot].arg = arg;
        if (total_recorded >= Capacity) {
            ++overwritten;
        }
        ++total_recorded;
    }

    /// Number of events currently retained in the ring (0..Capacity).
    size_t count() const {
        return total_recorded < Capacity ? static_cast<size_t>(total_recorded) : Capacity;
    }

    /// Read an event by oldest-first offset (0 = oldest retained, count()-1 = newest).
    bool read_oldest(size_t offset, Event& out) const {
        const size_t n = count();
        if (offset >= n) return false;
        const size_t oldest = (total_recorded >= Capacity)
                                  ? (static_cast<size_t>(total_recorded) & (Capacity - 1u))
                                  : 0u;
        const size_t idx = (oldest + offset) & (Capacity - 1u);
        out = entries[idx];
        return true;
    }

    /// Serialize DumpHeader + oldest-to-newest retained events into caller buffer.
    /// Returns total bytes written, or 0 if out is null or max_bytes < sizeof(DumpHeader).
    size_t serialize(uint8_t* out, size_t max_bytes) const {
        if (!out || max_bytes < sizeof(DumpHeader)) return 0;
        const size_t n = count();
        const size_t max_events = (max_bytes - sizeof(DumpHeader)) / sizeof(Event);
        const size_t emit_n = n < max_events ? n : max_events;
        const size_t start_offset = n - emit_n; // Prefer most recent events if buffer is smaller.

        auto put_u32 = [](uint8_t* dst, uint32_t v) {
            dst[0] = static_cast<uint8_t>(v & 0xffu);
            dst[1] = static_cast<uint8_t>((v >> 8) & 0xffu);
            dst[2] = static_cast<uint8_t>((v >> 16) & 0xffu);
            dst[3] = static_cast<uint8_t>((v >> 24) & 0xffu);
        };
        auto put_u64 = [&put_u32](uint8_t* dst, uint64_t v) {
            put_u32(dst, static_cast<uint32_t>(v & 0xffffffffu));
            put_u32(dst + 4, static_cast<uint32_t>(v >> 32));
        };

        put_u32(out + 0, dump_magic);
        put_u32(out + 4, dump_version);
        put_u32(out + 8, static_cast<uint32_t>(emit_n));
        put_u32(out + 12, total_recorded);

        size_t pos = sizeof(DumpHeader);
        for (size_t i = 0; i < emit_n; ++i) {
            Event ev{};
            (void)read_oldest(start_offset + i, ev);
            put_u64(out + pos + 0, ev.timestamp);
            out[pos + 8] = static_cast<uint8_t>(ev.event_id & 0xffu);
            out[pos + 9] = static_cast<uint8_t>((ev.event_id >> 8) & 0xffu);
            out[pos + 10] = static_cast<uint8_t>(ev.source_id & 0xffu);
            out[pos + 11] = static_cast<uint8_t>((ev.source_id >> 8) & 0xffu);
            put_u32(out + pos + 12, ev.arg);
            pos += sizeof(Event);
        }
        return pos;
    }

    /// Clear all recorded events.
    void clear() {
        total_recorded = 0;
        overwritten = 0;
    }
};

} // namespace rtos::trace
