#pragma once
/** @file rtos_hw_sched.hpp
 * @brief MMIO contract, driver, and bit-exact host reference model for NexRT-HWSched.
 *
 * Matches rtl/nexrt_hw_sched.sv register-for-register:
 *   - Global control/status window: base + 0x000 .. base + 0x03F (addr[9] == 0)
 *   - Per-thread register bank:     base + 0x200 + tid * 0x20    (addr[9] == 1, tid = addr[8:5])
 *
 * Static-only, zero heap, single-hart deterministic hardware arbitration.
 */
#include <stddef.h>
#include <stdint.h>
#include "rtos.hpp"

namespace rtos::hw_sched {

inline constexpr uint32_t kVersion = 0x4E585301u; // "NXS\x01"
inline constexpr unsigned kMaxThreads = 16;
inline constexpr unsigned kMaxIrqRoutes = 4;

// Global register offsets (addr[9] == 0)
inline constexpr uint32_t kRegCtrl        = 0x000u;
inline constexpr uint32_t kRegStatus      = 0x004u;
inline constexpr uint32_t kRegCommit      = 0x008u;
inline constexpr uint32_t kRegQuantum     = 0x00Cu;
inline constexpr uint32_t kRegExpiredMask = 0x010u;
inline constexpr uint32_t kRegReadyMask   = 0x014u;
inline constexpr uint32_t kRegFaultMask   = 0x018u;
inline constexpr uint32_t kRegSeuCount    = 0x01Cu;
inline constexpr uint32_t kRegIrqRoute0   = 0x020u;
inline constexpr uint32_t kRegMtimeLo     = 0x030u;
inline constexpr uint32_t kRegMtimeHi     = 0x034u;
inline constexpr uint32_t kRegSliceRemain = 0x038u;
inline constexpr uint32_t kRegVersion     = 0x03Cu;

// Per-thread bank offsets (addr[9] == 1, tid = addr[8:5], reg = addr[4:2])
inline constexpr uint32_t kThreadBankBase = 0x200u;
inline constexpr uint32_t kThreadStride   = 0x020u;
inline constexpr uint32_t kThrStatePrio   = 0x00u;
inline constexpr uint32_t kThrWakeLo      = 0x04u;
inline constexpr uint32_t kThrWakeHi      = 0x08u;
inline constexpr uint32_t kThrBudget      = 0x0Cu;
inline constexpr uint32_t kThrRuntimeLo   = 0x10u;
inline constexpr uint32_t kThrRuntimeHi   = 0x14u;
inline constexpr uint32_t kThrDispatches  = 0x18u;
inline constexpr uint32_t kThrSliceUsed   = 0x1Cu;

// CTRL bits
inline constexpr uint32_t kCtrlEnable   = 1u << 0;
inline constexpr uint32_t kCtrlTickless = 1u << 1;
inline constexpr uint32_t kCtrlRrEnable = 1u << 2;
inline constexpr uint32_t kCtrlSealed   = 1u << 3;
inline constexpr uint32_t kCtrlIrqEn    = 1u << 4;

// STATUS fields
inline constexpr uint32_t kStatusCurrentTidMask = 0x0000000Fu;
inline constexpr uint32_t kStatusCurrentValid   = 1u << 4;
inline constexpr uint32_t kStatusNextTidShift   = 8u;
inline constexpr uint32_t kStatusNextTidMask    = 0x00000F00u;
inline constexpr uint32_t kStatusNextValid      = 1u << 12;
inline constexpr uint32_t kStatusPreemptReq     = 1u << 13;
inline constexpr uint32_t kStatusSliceExpired   = 1u << 14;
inline constexpr uint32_t kStatusBudgetFault    = 1u << 15;
inline constexpr uint32_t kStatusSeuFault       = 1u << 16;

// COMMIT bits
inline constexpr uint32_t kCommitSwitch = 1u << 0;
inline constexpr uint32_t kCommitRotate = 1u << 1;

// IRQ_ROUTE bits
inline constexpr uint32_t kIrqRouteTidMask = 0x0000000Fu;
inline constexpr uint32_t kIrqRouteEnable  = 1u << 4;
inline constexpr uint32_t kIrqRoutePending = 1u << 8;

inline constexpr uint32_t thread_offset(uint8_t tid, uint32_t reg) {
    return kThreadBankBase + (static_cast<uint32_t>(tid) * kThreadStride) + reg;
}

inline constexpr uint32_t pack_state_prio(State state, uint8_t base_prio, uint8_t eff_prio) {
    return (static_cast<uint32_t>(state) & 0x7u) |
           (static_cast<uint32_t>(base_prio) << 8) |
           (static_cast<uint32_t>(eff_prio) << 16);
}

inline constexpr State unpack_state(uint32_t word) {
    return static_cast<State>(word & 0x7u);
}

inline constexpr uint8_t unpack_base_prio(uint32_t word) {
    return static_cast<uint8_t>((word >> 8) & 0xFFu);
}

inline constexpr uint8_t unpack_eff_prio(uint32_t word) {
    return static_cast<uint8_t>((word >> 16) & 0xFFu);
}

struct StatusSnapshot {
    uint8_t current_tid{};
    bool current_valid{};
    uint8_t next_tid{};
    bool next_valid{};
    bool preempt_req{};
    bool slice_expired{};
    bool budget_fault{};
    bool seu_fault{};
};

inline constexpr StatusSnapshot decode_status(uint32_t raw) {
    return {
        static_cast<uint8_t>(raw & kStatusCurrentTidMask),
        (raw & kStatusCurrentValid) != 0,
        static_cast<uint8_t>((raw & kStatusNextTidMask) >> kStatusNextTidShift),
        (raw & kStatusNextValid) != 0,
        (raw & kStatusPreemptReq) != 0,
        (raw & kStatusSliceExpired) != 0,
        (raw & kStatusBudgetFault) != 0,
        (raw & kStatusSeuFault) != 0,
    };
}

/** @brief Direct MMIO driver for NexRT-HWSched on real RISCue hardware. */
class MmioDriver {
    uintptr_t base_;
    volatile uint32_t& reg(uint32_t offset) const {
        return *reinterpret_cast<volatile uint32_t*>(base_ + offset);
    }
public:
    explicit constexpr MmioDriver(uintptr_t base = RTOS_HWSCHED_BASE) : base_(base) {}
    uint32_t read(uint32_t offset) const { return reg(offset); }
    void write(uint32_t offset, uint32_t value) const { reg(offset) = value; }
    uint32_t version() const { return read(kRegVersion); }
    StatusSnapshot status() const { return decode_status(read(kRegStatus)); }
    uint32_t expired_mask() const { return read(kRegExpiredMask); }
    void clear_expired(uint32_t mask) const { write(kRegExpiredMask, mask); }
    uint32_t fault_mask() const { return read(kRegFaultMask); }
    void clear_faults(uint32_t mask) const { write(kRegFaultMask, mask); }
    uint32_t ready_mask() const { return read(kRegReadyMask); }
    uint32_t seu_count() const { return read(kRegSeuCount); }

    void configure_thread(uint8_t tid, State state, uint8_t base_prio, uint8_t eff_prio) const {
        write(thread_offset(tid, kThrStatePrio), pack_state_prio(state, base_prio, eff_prio));
    }
    void set_wake(uint8_t tid, uint64_t absolute_cycle) const {
        write(thread_offset(tid, kThrWakeHi), 0xFFFFFFFFu);
        write(thread_offset(tid, kThrWakeLo), static_cast<uint32_t>(absolute_cycle));
        write(thread_offset(tid, kThrWakeHi), static_cast<uint32_t>(absolute_cycle >> 32));
    }
    void set_budget(uint8_t tid, uint32_t cycles) const {
        write(thread_offset(tid, kThrBudget), cycles);
    }
    void configure_irq_route(uint8_t irq_line, uint8_t tid, bool enable) const {
        if (irq_line >= kMaxIrqRoutes) return;
        write(kRegIrqRoute0 + static_cast<uint32_t>(irq_line) * 4u,
              (static_cast<uint32_t>(tid) & kIrqRouteTidMask) | (enable ? kIrqRouteEnable : 0u));
    }
    uint64_t mtime() const {
        uint32_t hi = 0;
        uint32_t lo = 0;
        uint32_t again = 0;
        do {
            hi = read(kRegMtimeHi);
            lo = read(kRegMtimeLo);
            again = read(kRegMtimeHi);
        } while (hi != again);
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    uint64_t runtime(uint8_t tid) const {
        uint32_t hi = 0;
        uint32_t lo = 0;
        uint32_t again = 0;
        do {
            hi = read(thread_offset(tid, kThrRuntimeHi));
            lo = read(thread_offset(tid, kThrRuntimeLo));
            again = read(thread_offset(tid, kThrRuntimeHi));
        } while (hi != again);
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    uint32_t dispatches(uint8_t tid) const {
        return read(thread_offset(tid, kThrDispatches));
    }
    void start(uint32_t quantum_cycles, bool tickless, bool round_robin) const {
        write(kRegQuantum, quantum_cycles);
        uint32_t ctrl = kCtrlEnable | kCtrlSealed | kCtrlIrqEn;
        if (tickless) ctrl |= kCtrlTickless;
        if (round_robin) ctrl |= kCtrlRrEnable;
        write(kRegCtrl, ctrl);
    }
    void commit(bool rotate) const {
        write(kRegCommit, kCommitSwitch | (rotate ? kCommitRotate : 0u));
    }
};

/** @brief Cycle-accurate C++ reference oracle matching rtl/nexrt_hw_sched.sv.
 * Used for host verification and lockstep Verilator co-simulation.
 */
class Model {
    struct Slot {
        State state{State::unused};
        uint8_t base_prio{};
        uint8_t eff_prio{};
        uint64_t wake{UINT64_MAX};
        uint32_t budget{};
        uint64_t runtime{};
        uint32_t dispatches{};
        uint32_t slice_used{};
    };
    struct Route {
        uint8_t tid{};
        bool enable{};
        bool pending{};
    };
    Slot slots_[kMaxThreads]{};
    Route routes_[kMaxIrqRoutes]{};
    uint8_t order_[kMaxThreads]{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint32_t ctrl_{};
    uint32_t quantum_{};
    uint32_t slice_remain_{};
    uint32_t expired_mask_{};
    uint32_t fault_mask_{};
    uint32_t seu_count_{};
    uint64_t mtime_{};
    uint8_t current_tid_{};
    bool current_valid_{};
    bool slice_expired_{};
    bool seu_fault_{};

    void move_to_back(uint8_t tid) {
        unsigned pos = 0;
        while (pos < kMaxThreads && order_[pos] != tid) ++pos;
        if (pos >= kMaxThreads) return;
        for (unsigned i = pos; i + 1 < kMaxThreads; ++i) order_[i] = order_[i + 1];
        order_[kMaxThreads - 1] = tid;
    }

    void arbitrate(uint8_t& next_tid, bool& next_valid) const {
        next_tid = 0;
        next_valid = false;
        uint8_t best_prio = 0;
        for (unsigned i = 0; i < kMaxThreads; ++i) {
            const uint8_t tid = order_[i];
            if (slots_[tid].state == State::ready &&
                (!next_valid || slots_[tid].eff_prio > best_prio)) {
                next_valid = true;
                next_tid = tid;
                best_prio = slots_[tid].eff_prio;
            }
        }
    }

    bool ready_peers_at_current() const {
        if (!current_valid_ || slots_[current_tid_].state != State::ready) return false;
        for (unsigned i = 0; i < kMaxThreads; ++i) {
            if (i != current_tid_ &&
                slots_[i].state == State::ready &&
                slots_[i].eff_prio == slots_[current_tid_].eff_prio) {
                return true;
            }
        }
        return false;
    }

public:
    Model() = default;

    void reset() {
        *this = Model{};
    }

    bool sched_irq() const {
        if ((ctrl_ & kCtrlEnable) == 0 || (ctrl_ & kCtrlIrqEn) == 0) return false;
        return decode_status(read(kRegStatus)).preempt_req;
    }

    uint32_t read(uint32_t offset) const {
        if ((offset & 0x3u) != 0) return 0;
        if ((offset & 0x200u) == 0) {
            switch (offset & 0xFFu) {
            case kRegCtrl: return ctrl_;
            case kRegStatus: {
                uint8_t next_tid = 0;
                bool next_valid = false;
                arbitrate(next_tid, next_valid);
                const bool current_ready = current_valid_ && slots_[current_tid_].state == State::ready;
                const bool higher_ready = next_valid && (!current_ready ||
                    slots_[next_tid].eff_prio > slots_[current_tid_].eff_prio);
                const bool rotate_due = slice_expired_ && ((ctrl_ & kCtrlRrEnable) != 0) &&
                    (((ctrl_ & kCtrlTickless) == 0) || ready_peers_at_current());
                const bool preempt = ((ctrl_ & kCtrlEnable) != 0) &&
                    (higher_ready || (!current_ready && next_valid) ||
                     (current_valid_ && !current_ready) ||
                     rotate_due || expired_mask_ != 0 || fault_mask_ != 0);
                uint32_t status = (static_cast<uint32_t>(current_tid_) & 0xFu) |
                                  (current_valid_ ? kStatusCurrentValid : 0u) |
                                  (static_cast<uint32_t>(next_tid) << kStatusNextTidShift) |
                                  (next_valid ? kStatusNextValid : 0u);
                if (preempt) status |= kStatusPreemptReq;
                if (slice_expired_) status |= kStatusSliceExpired;
                if (fault_mask_ != 0) status |= kStatusBudgetFault;
                if (seu_fault_) status |= kStatusSeuFault;
                return status;
            }
            case kRegQuantum: return quantum_;
            case kRegExpiredMask: return expired_mask_;
            case kRegReadyMask: {
                uint32_t mask = 0;
                for (unsigned i = 0; i < kMaxThreads; ++i) {
                    if (slots_[i].state == State::ready) mask |= (1u << i);
                }
                return mask;
            }
            case kRegFaultMask: return fault_mask_;
            case kRegSeuCount: return seu_count_;
            case kRegMtimeLo: return static_cast<uint32_t>(mtime_);
            case kRegMtimeHi: return static_cast<uint32_t>(mtime_ >> 32);
            case kRegSliceRemain: return slice_remain_;
            case kRegVersion: return kVersion;
            default: {
                const uint32_t local = offset & 0xFFu;
                if (local >= kRegIrqRoute0 && local < kRegIrqRoute0 + kMaxIrqRoutes * 4u) {
                    const unsigned idx = (local - kRegIrqRoute0) / 4u;
                    return (static_cast<uint32_t>(routes_[idx].tid) & 0xFu) |
                           (routes_[idx].enable ? kIrqRouteEnable : 0u) |
                           (routes_[idx].pending ? kIrqRoutePending : 0u);
                }
                return 0;
            }
            }
        }
        const uint8_t tid = static_cast<uint8_t>((offset >> 5) & 0xFu);
        const uint32_t field = offset & 0x1Cu;
        const auto& s = slots_[tid];
        switch (field) {
        case kThrStatePrio: return pack_state_prio(s.state, s.base_prio, s.eff_prio);
        case kThrWakeLo: return static_cast<uint32_t>(s.wake);
        case kThrWakeHi: return static_cast<uint32_t>(s.wake >> 32);
        case kThrBudget: return s.budget;
        case kThrRuntimeLo: return static_cast<uint32_t>(s.runtime);
        case kThrRuntimeHi: return static_cast<uint32_t>(s.runtime >> 32);
        case kThrDispatches: return s.dispatches;
        case kThrSliceUsed: return s.slice_used;
        default: return 0;
        }
    }

    void write(uint32_t offset, uint32_t value) {
        if ((offset & 0x3u) != 0) return;
        if ((offset & 0x200u) == 0) {
            switch (offset & 0xFFu) {
            case kRegCtrl: {
                const bool was_sealed = (ctrl_ & kCtrlSealed) != 0;
                ctrl_ = value & 0x1Fu;
                if (was_sealed) ctrl_ |= kCtrlSealed;
                if ((ctrl_ & kCtrlEnable) != 0 && slice_remain_ == 0 && quantum_ != 0) {
                    slice_remain_ = quantum_;
                }
                break;
            }
            case kRegCommit: {
                if ((value & kCommitRotate) != 0 && current_valid_) {
                    move_to_back(current_tid_);
                }
                if ((value & kCommitSwitch) != 0) {
                    uint8_t next_tid = 0;
                    bool next_valid = false;
                    arbitrate(next_tid, next_valid);
                    if (current_valid_ && (!next_valid || next_tid != current_tid_)) {
                        slots_[current_tid_].slice_used = 0;
                    }
                    current_tid_ = next_tid;
                    current_valid_ = next_valid;
                    if (next_valid) {
                        ++slots_[next_tid].dispatches;
                        slots_[next_tid].slice_used = 0;
                    }
                    slice_remain_ = quantum_;
                    slice_expired_ = false;
                }
                break;
            }
            case kRegQuantum:
                quantum_ = value;
                if (slice_remain_ == 0 || slice_remain_ > quantum_) slice_remain_ = quantum_;
                break;
            case kRegExpiredMask:
                expired_mask_ &= ~value;
                break;
            case kRegFaultMask:
                fault_mask_ &= ~value;
                break;
            default: {
                const uint32_t local = offset & 0xFFu;
                if (local >= kRegIrqRoute0 && local < kRegIrqRoute0 + kMaxIrqRoutes * 4u) {
                    const unsigned idx = (local - kRegIrqRoute0) / 4u;
                    if ((value & kIrqRoutePending) != 0) routes_[idx].pending = false;
                    if ((ctrl_ & kCtrlSealed) == 0) {
                        routes_[idx].tid = static_cast<uint8_t>(value & 0xFu);
                        routes_[idx].enable = (value & kIrqRouteEnable) != 0;
                    }
                }
                break;
            }
            }
            return;
        }
        const uint8_t tid = static_cast<uint8_t>((offset >> 5) & 0xFu);
        const uint32_t field = offset & 0x1Cu;
        auto& s = slots_[tid];
        switch (field) {
        case kThrStatePrio: {
            const State next_state = unpack_state(value);
            if ((ctrl_ & kCtrlSealed) != 0 && s.state == State::unused && next_state != State::unused) {
                return; // Sealed registry rejects post-boot slot registration.
            }
            s.state = next_state;
            s.base_prio = unpack_base_prio(value);
            s.eff_prio = unpack_eff_prio(value);
            if (next_state != State::ready) s.slice_used = 0;
            break;
        }
        case kThrWakeLo:
            s.wake = (s.wake & 0xFFFFFFFF00000000ull) | value;
            break;
        case kThrWakeHi:
            s.wake = (s.wake & 0x00000000FFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
            break;
        case kThrBudget:
            s.budget = value;
            break;
        default:
            break;
        }
    }

    /** @brief Advance hardware clock by one cycle with optional peripheral IRQ lines. */
    void step(uint8_t periph_irq_mask = 0) {
        ++mtime_;
        if ((ctrl_ & kCtrlEnable) == 0) return;

        // 1. Hardware peripheral IRQ-to-thread wake routing (1-cycle wake).
        for (unsigned k = 0; k < kMaxIrqRoutes; ++k) {
            if (((periph_irq_mask >> k) & 1u) != 0 && routes_[k].enable) {
                routes_[k].pending = true;
                auto& target = slots_[routes_[k].tid];
                if (target.state == State::sleeping || target.state == State::blocked) {
                    target.state = State::ready;
                }
            }
        }

        // 2. Parallel deadline comparator bank against mtime_.
        for (unsigned i = 0; i < kMaxThreads; ++i) {
            auto& s = slots_[i];
            if ((s.state == State::sleeping || s.state == State::blocked) && mtime_ >= s.wake) {
                expired_mask_ |= (1u << i);
                if (s.state == State::sleeping) {
                    s.state = State::ready;
                }
            }
        }

        // 3. Hardware residency accounting, slice quantum, and WCET budget watchdog.
        if (current_valid_ && slots_[current_tid_].state == State::ready) {
            auto& cur = slots_[current_tid_];
            ++cur.runtime;
            ++cur.slice_used;
            if (cur.budget != 0 && cur.slice_used > cur.budget) {
                cur.state = State::quarantined;
                fault_mask_ |= (1u << current_tid_);
            }
            if ((ctrl_ & kCtrlRrEnable) != 0 && quantum_ != 0) {
                if (slice_remain_ > 1) {
                    --slice_remain_;
                } else {
                    slice_remain_ = 0;
                    slice_expired_ = true;
                }
            }
        }
    }
};

} // namespace rtos::hw_sched
