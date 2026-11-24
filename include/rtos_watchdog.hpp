#pragma once
/** @file rtos_watchdog.hpp
 * @brief Cooperative hardware watchdog supervisor with CRC-32C persistent crash record.
 *
 * Every registered critical task checks in within its deadline. evaluate(now) pets
 * the hardware watchdog ONLY if every active task is fresh. On the first missed
 * deadline, evaluate() withholds the hardware kick and seals a CRC-32C CrashRecord
 * into caller-supplied retained SRAM so post-reset boot telemetry knows which task
 * stalled and when.
 */
#include "rtos.hpp"
#include <stddef.h>
#include <stdint.h>

namespace rtos::watchdog {

inline constexpr uint32_t kCrashMagic = 0x4E584352u; // "NXCR"

/// Castagnoli CRC-32C (compact bitwise, zero RAM LUT).
inline uint32_t crc32c(const void* data, size_t bytes, uint32_t seed = 0xFFFFFFFFu) {
    uint32_t crc = seed;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < bytes; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0x82F63B78u) : (crc >> 1);
        }
    }
    return ~crc;
}

/// Retained-RAM crash record surviving warm watchdog resets.
struct CrashRecord {
    uint32_t magic{};
    uint32_t fault_code{};     // 1 = task watchdog timeout, 2 = kernel/stack fault
    uint32_t stalled_task_id{};
    Tick fault_tick{};
    Tick last_checkin_tick{};
    uint32_t crc32{};

    void seal() {
        magic = kCrashMagic;
        crc32 = crc32c(this, sizeof(CrashRecord) - sizeof(uint32_t));
    }

    bool valid() const {
        return magic == kCrashMagic &&
               crc32 == crc32c(this, sizeof(CrashRecord) - sizeof(uint32_t));
    }

    void clear() {
        magic = 0;
        fault_code = 0;
        stalled_task_id = 0;
        fault_tick = 0;
        last_checkin_tick = 0;
        crc32 = 0;
    }
};

using PetCallback = void (*)(void*);

template<size_t MaxTasks>
class Supervisor {
    static_assert(MaxTasks >= 1 && MaxTasks <= 32, "MaxTasks must be 1..32");
    struct Entry {
        Tick max_interval{};
        Tick last_checkin{};
        bool active{};
    };
    Entry tasks_[MaxTasks]{};
    PetCallback pet_{};
    void* pet_context_{};
    CrashRecord* retained_{};
    bool tripped_{};
    size_t stalled_id_{};

public:
    constexpr Supervisor() = default;
    Supervisor(const Supervisor&) = delete;
    Supervisor& operator=(const Supervisor&) = delete;

    bool initialize(PetCallback pet_hw_wdt, void* context, CrashRecord* retained_record) {
        if (!pet_hw_wdt || !retained_record) return false;
        pet_ = pet_hw_wdt;
        pet_context_ = context;
        retained_ = retained_record;
        tripped_ = false;
        stalled_id_ = 0;
        return true;
    }

    bool register_task(size_t& task_id, Tick max_interval, Tick now) {
        if (max_interval == 0 || max_interval > 0x7fffffffu) return false;
        for (size_t i = 0; i < MaxTasks; ++i) {
            if (!tasks_[i].active) {
                tasks_[i] = {max_interval, now, true};
                task_id = i;
                return true;
            }
        }
        return false;
    }

    bool checkin(size_t task_id, Tick now) {
        if (task_id >= MaxTasks || !tasks_[task_id].active) return false;
        tasks_[task_id].last_checkin = now;
        return true;
    }

    /** @brief Pets HW watchdog iff every registered task checked in within budget.
     * On timeout, withholds pet, writes a CRC-32C CrashRecord, and returns false.
     */
    bool evaluate(Tick now) {
        if (!pet_ || !retained_ || tripped_) return false;
        for (size_t i = 0; i < MaxTasks; ++i) {
            if (!tasks_[i].active) continue;
            const Tick elapsed = static_cast<Tick>(now - tasks_[i].last_checkin);
            if (elapsed > tasks_[i].max_interval) {
                tripped_ = true;
                stalled_id_ = i;
                retained_->fault_code = 1u;
                retained_->stalled_task_id = static_cast<uint32_t>(i);
                retained_->fault_tick = now;
                retained_->last_checkin_tick = tasks_[i].last_checkin;
                retained_->seal();
                return false;
            }
        }
        pet_(pet_context_);
        return true;
    }

    bool tripped() const { return tripped_; }
    size_t stalled_task() const { return stalled_id_; }
};

} // namespace rtos::watchdog
