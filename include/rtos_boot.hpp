#pragma once
/** @file rtos_boot.hpp
 * @brief Retained-SRAM BootHandoff ABI and RTEMS-style HealthManager for
 *        separate-bootloader Safe-Mode fallback and fault capture (zero heap).
 *
 * See docs/bootloader-spec.rst for the immutable bootloader contract.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos_watchdog.hpp"

namespace rtos::boot {

inline constexpr uint32_t kHandoffMagic = 0x4E584248u; // "NXBH"
inline constexpr uint16_t kHandoffVersion = 1u;

inline constexpr uint32_t kFlagSafeMode = 1u << 0;
inline constexpr uint32_t kFlagConfirmedHealthy = 1u << 1;
inline constexpr uint32_t kFlagCrashLogged = 1u << 2;

enum class BootReason : uint8_t {
    power_on = 0,
    watchdog = 1,
    cpu_fault = 2,
    ground_command = 3,
    brownout = 4
};

enum class BootSlot : uint8_t {
    golden = 0,
    flight_a = 1,
    flight_b = 2
};

/// Fixed 48-byte zero-padding retained-SRAM handoff record shared with the bootloader.
struct BootHandoff {
    uint32_t magic{};
    uint16_t version{};
    uint8_t boot_reason{};
    uint8_t active_slot{};
    uint16_t boot_attempts{};
    uint16_t max_boot_attempts{3u};
    uint32_t flags{};
    uint32_t image_build_id{};
    uint32_t boot_count{};
    uint32_t last_fault_pc{};
    uint32_t last_fault_code{};
    uint64_t total_uptime_ticks{};
    uint32_t reserved{};
    uint32_t crc32{};

    void seal() {
        magic = kHandoffMagic;
        version = kHandoffVersion;
        crc32 = rtos::watchdog::crc32c(this, offsetof(BootHandoff, crc32));
    }

    bool valid() const {
        return magic == kHandoffMagic &&
               version == kHandoffVersion &&
               crc32 == rtos::watchdog::crc32c(this, offsetof(BootHandoff, crc32));
    }

    bool safe_mode() const { return (flags & kFlagSafeMode) != 0u; }
    bool confirmed_healthy() const { return (flags & kFlagConfirmedHealthy) != 0u; }
};
static_assert(sizeof(BootHandoff) == 48u, "BootHandoff ABI must remain fixed at 48 bytes");
static_assert(offsetof(BootHandoff, crc32) == 44u, "crc32 must be the final 4 bytes");

/** @brief Simulates/executes the bootloader-side boot-attempt and Safe-Mode state transition. */
inline void bootloader_prepare_handoff(BootHandoff& h, BootReason reason, uint32_t build_id,
                                       uint16_t max_attempts = 3u) {
    if (!h.valid() || reason == BootReason::power_on) {
        h = {};
        h.boot_reason = static_cast<uint8_t>(reason);
        h.active_slot = static_cast<uint8_t>(BootSlot::flight_a);
        h.boot_attempts = 1u;
        h.max_boot_attempts = max_attempts > 0u ? max_attempts : 1u;
        h.image_build_id = build_id;
        h.boot_count = 1u;
        h.seal();
        return;
    }
    h.boot_reason = static_cast<uint8_t>(reason);
    h.flags &= ~kFlagConfirmedHealthy;
    ++h.boot_count;
    if (reason == BootReason::watchdog || reason == BootReason::cpu_fault || reason == BootReason::brownout) {
        ++h.boot_attempts;
        if (h.boot_attempts > h.max_boot_attempts) {
            h.flags |= kFlagSafeMode;
            h.active_slot = static_cast<uint8_t>(BootSlot::golden);
        }
    } else {
        h.boot_attempts = 1u;
    }
    h.image_build_id = build_id;
    h.seal();
}

/** @brief RTOS-side boot health and fatal fault manager (zero heap). */
class HealthManager {
    BootHandoff* handoff_{};
    rtos::watchdog::CrashRecord* crash_{};

public:
    constexpr HealthManager() = default;

    bool initialize(BootHandoff* retained_handoff, rtos::watchdog::CrashRecord* retained_crash) {
        if (!retained_handoff || !retained_crash) return false;
        handoff_ = retained_handoff;
        crash_ = retained_crash;
        if (!handoff_->valid()) {
            bootloader_prepare_handoff(*handoff_, BootReason::power_on, 0u, 3u);
        }
        return true;
    }

    /// Call once critical flight tasks pass startup Barrier & self-test to clear boot-loop counter.
    bool confirm_boot_healthy() {
        if (!handoff_ || !handoff_->valid()) return false;
        handoff_->boot_attempts = 0u;
        handoff_->flags |= kFlagConfirmedHealthy;
        handoff_->seal();
        return true;
    }

    /// Record a fatal fault snapshot into retained SRAM prior to warm reset.
    void record_fatal(uint32_t fault_code, uint32_t fault_pc, uint32_t stalled_task,
                      rtos::Tick now, rtos::Tick last_checkin) {
        if (crash_) {
            crash_->fault_code = fault_code;
            crash_->stalled_task_id = stalled_task;
            crash_->fault_tick = now;
            crash_->last_checkin_tick = last_checkin;
            crash_->seal();
        }
        if (handoff_ && handoff_->valid()) {
            handoff_->last_fault_code = fault_code;
            handoff_->last_fault_pc = fault_pc;
            handoff_->total_uptime_ticks += now;
            handoff_->flags |= kFlagCrashLogged;
            handoff_->seal();
        }
    }

    bool in_safe_mode() const { return handoff_ && handoff_->valid() && handoff_->safe_mode(); }
    BootSlot active_slot() const {
        return handoff_ ? static_cast<BootSlot>(handoff_->active_slot) : BootSlot::golden;
    }
    BootReason boot_reason() const {
        return handoff_ ? static_cast<BootReason>(handoff_->boot_reason) : BootReason::power_on;
    }
    const BootHandoff* handoff() const { return handoff_; }
    const rtos::watchdog::CrashRecord* crash_record() const { return crash_; }
};

} // namespace rtos::boot
