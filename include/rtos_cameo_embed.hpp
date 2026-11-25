#pragma once
/** @file cameo_embed_nexrt.hpp
 * @brief Zero-heap C++20 adapter binding cameo::embed::StaticVm to NexRT (jeffadickson/rtos).
 *
 * Requires the NexRT include directory (jeffadickson/rtos/include) on the compiler include path.
 * Obeys every NexRT static firmware contract:
 *   - Zero heap allocation (no malloc/free/new/delete)
 *   - Zero exceptions, zero RTTI, zero runtime C++ static constructors (100% .bss)
 *   - Boot-static ownership of VM storage, thread stack, queue/trace/watchdog bindings
 *   - Board MMIO/AXI window presets for RISCue UARTLite, NexRT-HWSched, and SRAM
 *   - Zero-heap runtime hot-loading over UART / NexRT Monitor (MonitorBridge + PackageReceiver)
 */

#include "cameo_embed_vm.hpp"
#include "rtos.hpp"
#include "rtos_watchdog.hpp"

#if RTOS_SYNC
#include "rtos_queue.hpp"
#include "rtos_safe.hpp"
#endif

#if RTOS_LOGGING
#include "rtos_log.hpp"
#endif

#if RTOS_TRACE
#include "rtos_trace.hpp"
#endif

#if RTOS_MONITOR
#include "rtos_monitor.hpp"
#endif

namespace cameo::nexrt {

/// Pre-built NexRT board MMIO/AXI window descriptors (explicit, opt-in per board).
namespace AxiWindowPresets {

/// RISCue / Xilinx AXI UARTLite 32-bit MMIO window (default base 0x10000000, 256 bytes).
[[nodiscard]] constexpr cameo::embed::MemoryWindow riscue_uartlite(
    uint64_t base = 0x10000000ULL, bool allow_write = true) noexcept {
    return {
        base, 0x100u,
        allow_write ? cameo::embed::MemoryPerms::kRwAxi : cameo::embed::MemoryPerms::kRoAxi,
        4u, 4u, true
    };
}

/// NexRT AXI4-Lite hardware scheduler peripheral window (RTOS_HWSCHED_BASE, 1024 bytes).
[[nodiscard]] constexpr cameo::embed::MemoryWindow nexrt_hwsched(
    uint64_t base = static_cast<uint64_t>(RTOS_HWSCHED_BASE), bool allow_write = false) noexcept {
    return {
        base, 0x400u,
        allow_write ? cameo::embed::MemoryPerms::kRwAxi : cameo::embed::MemoryPerms::kRoAxi,
        4u, 4u, true
    };
}

/// Byte/halfword/word/doubleword accessible static RAM/scratch window.
[[nodiscard]] constexpr cameo::embed::MemoryWindow ram_window(
    uint64_t base, uint64_t size, bool allow_write = true) noexcept {
    return {
        base, size,
        allow_write ? cameo::embed::MemoryPerms::kRwRam : cameo::embed::MemoryPerms::kRoRam,
        1u, 8u, true
    };
}

} // namespace AxiWindowPresets

/// Type-erased, zero-virtual (function-pointer) bindings for NexRT kernel & debug services.
struct HostServices {
    using LogWriteFn = bool (*)(void* ctx, const char* data, size_t len) noexcept;
    using WatchdogCheckinFn = bool (*)(void* ctx, size_t task_id, rtos::Tick now) noexcept;
    using QueueSendFn = int64_t (*)(void* ctx, int64_t value, rtos::Tick timeout) noexcept;
    using QueueRecvFn = bool (*)(void* ctx, int64_t& value_out, rtos::Tick timeout) noexcept;
    using QueueDepthFn = int64_t (*)(void* ctx) noexcept;
    using GpioReadFn = int64_t (*)(void* ctx, int64_t pin) noexcept;
    using GpioWriteFn = void (*)(void* ctx, int64_t pin, int64_t value) noexcept;
    using SnapshotFn = rtos::SystemInfo (*)(void* ctx, rtos::ThreadInfo* out, size_t cap) noexcept;
    using TraceRecordFn = bool (*)(void* ctx, uint64_t ts, uint16_t event_id, uint16_t source_id, uint32_t arg) noexcept;
    using BreakpointFn = void (*)(void* ctx, int64_t id) noexcept;

    static constexpr size_t kMaxQueues = 8u;
    static constexpr size_t kMaxNotifyTargets = 8u;
    static constexpr size_t kMaxSnapshotThreads = 16u;

    struct QueueBinding {
        void* ctx{nullptr};
        QueueSendFn send{nullptr};
        QueueRecvFn recv{nullptr};
        QueueDepthFn depth{nullptr};
    };

    void* log_ctx{nullptr};
    LogWriteFn log_write{nullptr};

    void* watchdog_ctx{nullptr};
    WatchdogCheckinFn watchdog_checkin{nullptr};

    void* gpio_ctx{nullptr};
    GpioReadFn gpio_read{nullptr};
    GpioWriteFn gpio_write{nullptr};

    void* snapshot_ctx{nullptr};
    SnapshotFn snapshot_fn{nullptr};

    void* trace_ctx{nullptr};
    TraceRecordFn trace_record{nullptr};

    void* breakpoint_ctx{nullptr};
    BreakpointFn on_breakpoint{nullptr};

    QueueBinding queues[kMaxQueues]{};
    rtos::Thread* notify_targets[kMaxNotifyTargets]{};

    /// Bind a NexRT watchdog::Supervisor<MaxTasks> without virtual dispatch or heap.
    template<size_t MaxTasks>
    void bind_watchdog(rtos::watchdog::Supervisor<MaxTasks>& sup) noexcept {
        watchdog_ctx = &sup;
        watchdog_checkin = [](void* c, size_t task_id, rtos::Tick now) noexcept -> bool {
            return static_cast<rtos::watchdog::Supervisor<MaxTasks>*>(c)->checkin(task_id, now);
        };
    }

    /// Bind custom thread-snapshot provider (defaults to rtos::snapshot when null).
    void bind_snapshot(SnapshotFn fn, void* ctx = nullptr) noexcept {
        snapshot_fn = fn;
        snapshot_ctx = ctx;
    }

    /// Bind a custom debug breakpoint callback for dbg.breakpoint(id).
    void bind_breakpoint(BreakpointFn fn, void* ctx = nullptr) noexcept {
        on_breakpoint = fn;
        breakpoint_ctx = ctx;
    }

#if RTOS_LOGGING
    /// Bind a NexRT Logger<Slots, MessageBytes> to rtos.log(), dbg.log_hex(), and dbg.hexdump().
    template<size_t Slots, size_t MessageBytes>
    void bind_logger(rtos::Logger<Slots, MessageBytes>& logger) noexcept {
        log_ctx = &logger;
        log_write = [](void* c, const char* data, size_t len) noexcept -> bool {
            auto r = static_cast<rtos::Logger<Slots, MessageBytes>*>(c)->write(data, len);
            return r.result == rtos::Result::ok;
        };
    }
#endif

#if RTOS_TRACE
    /// Bind a NexRT TraceRing<Capacity> to dbg.trace(event_id, arg).
    template<size_t Capacity>
    void bind_trace(rtos::trace::TraceRing<Capacity>& ring) noexcept {
        trace_ctx = &ring;
        trace_record = [](void* c, uint64_t ts, uint16_t eid, uint16_t src, uint32_t arg) noexcept -> bool {
            static_cast<rtos::trace::TraceRing<Capacity>*>(c)->record(ts, eid, src, arg);
            return true;
        };
    }
#endif

#if RTOS_SYNC
    /// Bind a NexRT Queue<int64_t, Capacity> to channel index 0..kMaxQueues-1.
    template<size_t Capacity>
    [[nodiscard]] bool bind_queue(size_t channel, rtos::Queue<int64_t, Capacity>& q) noexcept {
        if (channel >= kMaxQueues) return false;
        queues[channel].ctx = &q;
        queues[channel].send = [](void* c, int64_t val, rtos::Tick timeout) noexcept -> int64_t {
            return static_cast<int64_t>(
                static_cast<rtos::Queue<int64_t, Capacity>*>(c)->send(val, timeout));
        };
        queues[channel].recv = [](void* c, int64_t& out, rtos::Tick timeout) noexcept -> bool {
            return static_cast<rtos::Queue<int64_t, Capacity>*>(c)->receive(out, timeout) == rtos::Result::ok;
        };
        queues[channel].depth = [](void* c) noexcept -> int64_t {
            return static_cast<int64_t>(
                static_cast<rtos::Queue<int64_t, Capacity>*>(c)->snapshot().depth);
        };
        return true;
    }

    /// Bind a NexRT Thread* to rtos.notify(target_index, value).
    [[nodiscard]] bool bind_notify_target(size_t index, rtos::Thread& thread) noexcept {
        if (index >= kMaxNotifyTargets) return false;
        notify_targets[index] = &thread;
        return true;
    }
#endif

    /// Register all standard rtos.* and dbg.* syscalls and debug writer on a Cameo StaticVm instance.
    template<typename Vm>
    void install_into(Vm& vm) noexcept {
        using namespace cameo::embed;
        (void)vm.register_host(StandardHostId::Ticks, &cb_ticks, this);
        (void)vm.register_host(StandardHostId::Sleep, &cb_sleep, this);
        (void)vm.register_host(StandardHostId::Yield, &cb_yield, this);
        (void)vm.register_host(StandardHostId::Log, &cb_log, this);
        (void)vm.register_host(StandardHostId::LogStr, &cb_log_str, this);
        (void)vm.register_host(StandardHostId::WatchdogCheckin, &cb_watchdog_checkin, this);
        (void)vm.register_host(StandardHostId::QueueSend, &cb_queue_send, this);
        (void)vm.register_host(StandardHostId::QueueRecv, &cb_queue_recv, this);
        (void)vm.register_host(StandardHostId::Notify, &cb_notify, this);
        (void)vm.register_host(StandardHostId::GpioRead, &cb_gpio_read, this);
        (void)vm.register_host(StandardHostId::GpioWrite, &cb_gpio_write, this);
        (void)vm.register_host(StandardHostId::TaskCount, &cb_task_count, this);
        (void)vm.register_host(StandardHostId::TaskState, &cb_task_state, this);
        (void)vm.register_host(StandardHostId::TaskPriority, &cb_task_priority, this);
        (void)vm.register_host(StandardHostId::TaskStackBytes, &cb_task_stack_bytes, this);
        (void)vm.register_host(StandardHostId::TaskGuardOk, &cb_task_guard_ok, this);
        (void)vm.register_host(StandardHostId::TaskCycles, &cb_task_cycles, this);
        (void)vm.register_host(StandardHostId::QueueDepth, &cb_queue_depth, this);
        (void)vm.register_host(StandardHostId::DbgTrace, &cb_dbg_trace, this);
        (void)vm.register_host(StandardHostId::DbgBreakpoint, &cb_dbg_breakpoint, this);
        vm.set_debug_writer(&cb_debug_write, this);
    }

private:
    static bool cb_debug_write(void* u, const char* data, size_t len) noexcept {
        auto* self = static_cast<HostServices*>(u);
        if (self->log_write == nullptr) return false;
        return self->log_write(self->log_ctx, data, len);
    }

    rtos::SystemInfo take_snapshot(rtos::ThreadInfo* rows, size_t cap) const noexcept {
        if (snapshot_fn != nullptr) return snapshot_fn(snapshot_ctx, rows, cap);
        return rtos::snapshot(rows, cap);
    }

    static size_t format_tag_int(char* buf, size_t cap, cameo::embed::StringView tag, int64_t val) noexcept {
        size_t pos = 0;
        for (size_t i = 0; i < tag.size() && pos + 1 < cap; ++i) buf[pos++] = tag[i];
        if (pos + 1 < cap) buf[pos++] = '=';
        uint64_t mag = 0;
        if (val < 0) {
            if (pos + 1 < cap) buf[pos++] = '-';
            mag = 0u - static_cast<uint64_t>(val);
        } else {
            mag = static_cast<uint64_t>(val);
        }
        char digits[24]{};
        size_t d = 0;
        do {
            digits[d++] = static_cast<char>('0' + (mag % 10u));
            mag /= 10u;
        } while (mag != 0u && d < sizeof(digits));
        while (d > 0u && pos + 1 < cap) buf[pos++] = digits[--d];
        if (pos < cap) buf[pos++] = '\n';
        return pos;
    }

    static cameo::embed::Status cb_ticks(void*, cameo::embed::HostCallContext& ctx) noexcept {
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(rtos::ticks())));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_sleep(void*, cameo::embed::HostCallContext& ctx) noexcept {
        int64_t dt = 0;
        if (!ctx.try_int(0, dt) || dt < 0 || dt > 0x7FFFFFFFLL) {
            (void)ctx.push_result(cameo::embed::Value::from_bool(false));
            return cameo::embed::Status::Ok;
        }
        const bool ok = rtos::sleep_for(static_cast<rtos::Tick>(dt));
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_yield(void*, cameo::embed::HostCallContext&) noexcept {
        rtos::yield();
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_log(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        cameo::embed::StringView tag{};
        int64_t val = 0;
        if (!ctx.try_string(0, tag) || !ctx.try_int(1, val))
            return cameo::embed::Status::TypeError;
        bool ok = false;
        if (self->log_write != nullptr) {
            char line[96]{};
            const size_t n = format_tag_int(line, sizeof(line), tag, val);
            ok = self->log_write(self->log_ctx, line, n);
        }
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_log_str(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        cameo::embed::StringView msg{};
        if (!ctx.try_string(0, msg)) return cameo::embed::Status::TypeError;
        bool ok = false;
        if (self->log_write != nullptr) ok = self->log_write(self->log_ctx, msg.data(), msg.size());
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_watchdog_checkin(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t tid = 0;
        if (!ctx.try_int(0, tid) || tid < 0) return cameo::embed::Status::TypeError;
        bool ok = false;
        if (self->watchdog_checkin != nullptr) {
            ok = self->watchdog_checkin(self->watchdog_ctx, static_cast<size_t>(tid), rtos::ticks());
        }
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_queue_send(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t ch = 0, val = 0, timeout = 0;
        if (!ctx.try_int(0, ch) || !ctx.try_int(1, val)) return cameo::embed::Status::TypeError;
        if (ctx.arg_count() >= 3u) (void)ctx.try_int(2, timeout);
        int64_t rc = -1;
        if (ch >= 0 && static_cast<size_t>(ch) < kMaxQueues && self->queues[ch].send != nullptr) {
            const rtos::Tick t = (timeout > 0 && timeout <= 0x7FFFFFFFLL) ? static_cast<rtos::Tick>(timeout) : 0u;
            rc = self->queues[ch].send(self->queues[ch].ctx, val, t);
        }
        (void)ctx.push_result(cameo::embed::Value::from_int(rc));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_queue_recv(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t ch = 0, timeout = 0;
        if (!ctx.try_int(0, ch)) return cameo::embed::Status::TypeError;
        if (ctx.arg_count() >= 2u) (void)ctx.try_int(1, timeout);
        int64_t out = 0;
        bool ok = false;
        if (ch >= 0 && static_cast<size_t>(ch) < kMaxQueues && self->queues[ch].recv != nullptr) {
            const rtos::Tick t = (timeout > 0 && timeout <= 0x7FFFFFFFLL) ? static_cast<rtos::Tick>(timeout) : 0u;
            ok = self->queues[ch].recv(self->queues[ch].ctx, out, t);
        }
        (void)ctx.push_result(cameo::embed::Value::from_int(out));
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_queue_depth(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t ch = 0;
        if (!ctx.try_int(0, ch)) return cameo::embed::Status::TypeError;
        int64_t depth = 0;
        if (ch >= 0 && static_cast<size_t>(ch) < kMaxQueues && self->queues[ch].depth != nullptr) {
            depth = self->queues[ch].depth(self->queues[ch].ctx);
        }
        (void)ctx.push_result(cameo::embed::Value::from_int(depth));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_notify(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t target = 0, val = 0;
        if (!ctx.try_int(0, target) || !ctx.try_int(1, val)) return cameo::embed::Status::TypeError;
        int64_t rc = -1;
#if RTOS_SYNC
        if (target >= 0 && static_cast<size_t>(target) < kMaxNotifyTargets && self->notify_targets[target] != nullptr) {
            rc = static_cast<int64_t>(rtos::notify(*self->notify_targets[target], static_cast<uint32_t>(val)));
        }
#else
        (void)self;
#endif
        (void)ctx.push_result(cameo::embed::Value::from_int(rc));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_gpio_read(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t pin = 0;
        if (!ctx.try_int(0, pin)) return cameo::embed::Status::TypeError;
        const int64_t v = (self->gpio_read != nullptr) ? self->gpio_read(self->gpio_ctx, pin) : 0;
        (void)ctx.push_result(cameo::embed::Value::from_int(v));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_gpio_write(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t pin = 0, val = 0;
        if (!ctx.try_int(0, pin) || !ctx.try_int(1, val)) return cameo::embed::Status::TypeError;
        if (self->gpio_write != nullptr) self->gpio_write(self->gpio_ctx, pin, val);
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_count(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        const rtos::SystemInfo info = self->take_snapshot(nullptr, 0u);
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(info.required_threads)));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_state(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t idx = 0;
        if (!ctx.try_int(0, idx)) return cameo::embed::Status::TypeError;
        rtos::ThreadInfo rows[kMaxSnapshotThreads]{};
        const rtos::SystemInfo info = self->take_snapshot(rows, kMaxSnapshotThreads);
        const int64_t st = (idx >= 0 && static_cast<size_t>(idx) < info.written_threads)
            ? static_cast<int64_t>(rows[idx].state) : -1;
        (void)ctx.push_result(cameo::embed::Value::from_int(st));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_priority(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t idx = 0;
        if (!ctx.try_int(0, idx)) return cameo::embed::Status::TypeError;
        rtos::ThreadInfo rows[kMaxSnapshotThreads]{};
        const rtos::SystemInfo info = self->take_snapshot(rows, kMaxSnapshotThreads);
        const int64_t pr = (idx >= 0 && static_cast<size_t>(idx) < info.written_threads)
            ? static_cast<int64_t>(rows[idx].effective_priority) : -1;
        (void)ctx.push_result(cameo::embed::Value::from_int(pr));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_stack_bytes(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t idx = 0;
        if (!ctx.try_int(0, idx)) return cameo::embed::Status::TypeError;
        rtos::ThreadInfo rows[kMaxSnapshotThreads]{};
        const rtos::SystemInfo info = self->take_snapshot(rows, kMaxSnapshotThreads);
        const int64_t bytes = (idx >= 0 && static_cast<size_t>(idx) < info.written_threads)
            ? static_cast<int64_t>(rows[idx].stack_bytes) : 0;
        (void)ctx.push_result(cameo::embed::Value::from_int(bytes));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_guard_ok(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t idx = 0;
        if (!ctx.try_int(0, idx)) return cameo::embed::Status::TypeError;
        rtos::ThreadInfo rows[kMaxSnapshotThreads]{};
        const rtos::SystemInfo info = self->take_snapshot(rows, kMaxSnapshotThreads);
        const bool ok = (idx >= 0 && static_cast<size_t>(idx) < info.written_threads) && rows[idx].guard_ok;
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_task_cycles(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t idx = 0;
        if (!ctx.try_int(0, idx)) return cameo::embed::Status::TypeError;
        rtos::ThreadInfo rows[kMaxSnapshotThreads]{};
        const rtos::SystemInfo info = self->take_snapshot(rows, kMaxSnapshotThreads);
        const int64_t cyc = (idx >= 0 && static_cast<size_t>(idx) < info.written_threads)
            ? static_cast<int64_t>(rows[idx].runtime_cycles) : 0;
        (void)ctx.push_result(cameo::embed::Value::from_int(cyc));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_dbg_trace(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t eid = 0, arg = 0;
        if (!ctx.try_int(0, eid) || !ctx.try_int(1, arg)) return cameo::embed::Status::TypeError;
        bool ok = false;
        if (self->trace_record != nullptr) {
            ok = self->trace_record(self->trace_ctx, static_cast<uint64_t>(rtos::ticks()),
                                    static_cast<uint16_t>(eid), 0u, static_cast<uint32_t>(arg));
        }
        (void)ctx.push_result(cameo::embed::Value::from_bool(ok));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_dbg_breakpoint(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t id = 0;
        if (!ctx.try_int(0, id)) return cameo::embed::Status::TypeError;
        if (self->on_breakpoint != nullptr) self->on_breakpoint(self->breakpoint_ctx, id);
        return cameo::embed::Status::Ok;
    }
};

#if RTOS_SYNC
/**
 * @brief Boot-static periodic NexRT task executing an exported Cameo.Embed function
 *        every period_ticks with drift-free rtos::sleep_until() and O(1) per-tick arena reset.
 */
template<size_t StackBytes, uint8_t Priority, typename Vm>
class PeriodicScriptTask {
    rtos::safe::StaticTask<StackBytes, Priority> task_{};
    Vm vm_{};
    cameo::embed::Span<const uint8_t> image_{};
    const char* step_export_{nullptr};
    rtos::Tick period_ticks_{0u};
    uint32_t tick_step_budget_{0u};
    uint32_t init_step_budget_{0u};
    cameo::embed::Status last_status_{cameo::embed::Status::Ok};
    uint32_t ticks_executed_{0};
    bool initialized_{false};

    static void entry(void* arg) {
        auto* self = static_cast<PeriodicScriptTask*>(arg);
        self->run();
    }

    void run() {
        const auto st = vm_.load(image_);
        if (st != cameo::embed::Status::Ok) {
            last_status_ = st;
            rtos::exit();
        }
        const uint32_t init_b = init_step_budget_ > 0u ? init_step_budget_ : 100000u;
        const auto init_res = vm_.run_main(init_b);
        if (init_res.status != cameo::embed::Status::Ok) {
            last_status_ = init_res.status;
            rtos::exit();
        }
        cameo::embed::Value step_fn{};
        const char* exp_name = step_export_ != nullptr ? step_export_ : "step";
        if (!vm_.get_export(exp_name, step_fn)) {
            last_status_ = cameo::embed::Status::NotFound;
            rtos::exit();
        }
        const cameo::embed::PoolWatermark mark = vm_.mark_pools();
        initialized_ = true;
        rtos::Tick next_wake = rtos::ticks();
        for (;;) {
            const cameo::embed::Value tick_arg[1] = {
                cameo::embed::Value::from_int(static_cast<int64_t>(ticks_executed_))
            };
            cameo::embed::Value out[1]{};
            const uint32_t tick_b = tick_step_budget_ > 0u ? tick_step_budget_ : 50000u;
            const auto res = vm_.call_value(step_fn, tick_arg, out, tick_b);
            last_status_ = res.status;
            ++ticks_executed_;
            vm_.reset_pools_to(mark);
            (void)rtos::sleep_until(next_wake, period_ticks_ > 0u ? period_ticks_ : 1u);
        }
    }

public:
    constexpr PeriodicScriptTask() noexcept = default;
    PeriodicScriptTask(const PeriodicScriptTask&) = delete;
    PeriodicScriptTask& operator=(const PeriodicScriptTask&) = delete;

    [[nodiscard]] Vm& vm() noexcept { return vm_; }
    [[nodiscard]] const Vm& vm() const noexcept { return vm_; }
    [[nodiscard]] cameo::embed::Status last_status() const noexcept { return last_status_; }
    [[nodiscard]] uint32_t ticks_executed() const noexcept { return ticks_executed_; }
    [[nodiscard]] bool is_initialized() const noexcept { return initialized_; }

    /// Register this script task with NexRT at boot (before rtos::start()).
    [[nodiscard]] bool register_task(cameo::embed::Span<const uint8_t> cemb_image,
                                     rtos::Tick period_ticks,
                                     uint32_t tick_step_budget,
                                     const char* task_name,
                                     const char* step_export = "step") noexcept {
        image_ = cemb_image;
        period_ticks_ = period_ticks > 0u ? period_ticks : 1u;
        tick_step_budget_ = tick_step_budget;
        init_step_budget_ = 100000u;
        step_export_ = step_export != nullptr ? step_export : "step";
        return task_.register_task(&entry, this, task_name);
    }
};
#endif // RTOS_SYNC

#if RTOS_MONITOR
/**
 * @brief Zero-heap bridge connecting NexRT's diagnostic monitor `script` command to
 *        Cameo.Embed's PackageReceiver and StaticVm for live UART hot-loading and execution.
 *
 * Supported monitor subcommands:
 *   script reset                - clear hot-load staging buffer
 *   script hex <HEX...>         - append uppercase/lowercase hex bytes to staging buffer
 *   script load                 - verify CRC-32C, load CEMB package, run chunk main()
 *   script run <export> [arg0]  - invoke exported debug function with optional integer arg
 *   script status               - print loaded state, CRC-32C, staged bytes, last status
 */
template<typename Vm, size_t MaxPackageBytes = 4096>
class MonitorBridge {
    Vm* vm_{nullptr};
    cameo::embed::PackageReceiver<MaxPackageBytes> rx_{};
    cameo::embed::PoolWatermark post_main_mark_{};
    uint32_t step_budget_{0u};
    cameo::embed::Status last_status_{cameo::embed::Status::Ok};

    static void emit_sv(rtos::monitor::WriteFn emit, void* ctx, cameo::embed::StringView sv) noexcept {
        if (emit != nullptr) (void)emit(sv.data(), sv.size(), ctx);
    }

    static bool parse_int64(const char* s, int64_t& out) noexcept {
        if (s == nullptr || *s == '\0') return false;
        bool neg = false;
        if (*s == '-') { neg = true; ++s; }
        if (*s == '\0') return false;
        uint64_t v = 0;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            s += 2;
            if (*s == '\0') return false;
            while (*s != '\0') {
                const int n = cameo::embed::detail::hex_nibble(*s++);
                if (n < 0) return false;
                v = (v << 4u) | static_cast<uint64_t>(n);
            }
        } else {
            while (*s != '\0') {
                if (*s < '0' || *s > '9') return false;
                v = v * 10u + static_cast<uint64_t>(*s++ - '0');
            }
        }
        out = neg ? static_cast<int64_t>(0u - v) : static_cast<int64_t>(v);
        return true;
    }

    static void emit_hex32(rtos::monitor::WriteFn emit, void* ctx, uint32_t val) noexcept {
        char buf[10]{'0', 'x'};
        cameo::embed::detail::write_hex_digits(buf + 2, val, 8u);
        emit_sv(emit, ctx, cameo::embed::StringView(buf, 10u));
    }

    static void emit_i64(rtos::monitor::WriteFn emit, void* ctx, int64_t val) noexcept {
        char buf[24]{};
        size_t pos = 0;
        uint64_t mag = 0;
        if (val < 0) { buf[pos++] = '-'; mag = 0u - static_cast<uint64_t>(val); }
        else mag = static_cast<uint64_t>(val);
        char rev[24]{};
        size_t d = 0;
        do { rev[d++] = static_cast<char>('0' + (mag % 10u)); mag /= 10u; } while (mag != 0u);
        while (d > 0u) buf[pos++] = rev[--d];
        emit_sv(emit, ctx, cameo::embed::StringView(buf, pos));
    }

public:
    constexpr MonitorBridge() noexcept = default;

    void attach(Vm& vm, uint32_t default_step_budget = 50000u) noexcept {
        vm_ = &vm;
        step_budget_ = default_step_budget;
    }

    [[nodiscard]] cameo::embed::PackageReceiver<MaxPackageBytes>& receiver() noexcept { return rx_; }
    [[nodiscard]] cameo::embed::Status last_status() const noexcept { return last_status_; }

    /// Static callback compatible with rtos::monitor::Hooks::run_script.
    static bool hook_run_script(int argc, const char* const* argv,
                                rtos::monitor::WriteFn emit, void* emit_ctx, void* ctx) noexcept {
        auto* self = static_cast<MonitorBridge*>(ctx);
        if (self == nullptr || self->vm_ == nullptr) return false;
        return self->handle_command(argc, argv, emit, emit_ctx);
    }

    bool handle_command(int argc, const char* const* argv,
                        rtos::monitor::WriteFn emit, void* emit_ctx) noexcept {
        if (vm_ == nullptr) return false;
        if (argc < 2 || argv[1] == nullptr) {
            emit_sv(emit, emit_ctx, "usage: script reset|hex <H>|load|run <fn> [arg]|status\n");
            return false;
        }
        const cameo::embed::StringView sub(argv[1]);
        if (cameo::embed::detail::eq_sv(sub, "reset")) {
            rx_.reset();
            emit_sv(emit, emit_ctx, "ok reset\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "hex")) {
            if (argc < 3 || argv[2] == nullptr) {
                emit_sv(emit, emit_ctx, "err missing hex\n");
                return false;
            }
            for (int i = 2; i < argc; ++i) {
                if (!rx_.feed_hex(cameo::embed::StringView(argv[i]))) {
                    emit_sv(emit, emit_ctx, "err bad hex or overflow\n");
                    return false;
                }
            }
            emit_sv(emit, emit_ctx, "ok bytes=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(rx_.bytes_received()));
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "load")) {
            const auto vst = rx_.verify();
            if (vst != cameo::embed::Status::Ok) {
                last_status_ = vst;
                emit_sv(emit, emit_ctx, "err verify=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(vst));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            const auto lst = vm_->load(rx_.span());
            if (lst != cameo::embed::Status::Ok) {
                last_status_ = lst;
                emit_sv(emit, emit_ctx, "err load=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(lst));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            const auto mres = vm_->run_main(step_budget_);
            last_status_ = mres.status;
            if (mres.status != cameo::embed::Status::Ok) {
                emit_sv(emit, emit_ctx, "err main=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(mres.status));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            post_main_mark_ = vm_->mark_pools();
            emit_sv(emit, emit_ctx, "ok loaded crc=");
            emit_hex32(emit, emit_ctx, vm_->image_crc32c());
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "run")) {
            if (argc < 3 || argv[2] == nullptr) {
                emit_sv(emit, emit_ctx, "err missing export name\n");
                return false;
            }
            cameo::embed::Value args[4]{};
            size_t nargs = 0u;
            for (int i = 3; i < argc && nargs < 4u; ++i) {
                int64_t v = 0;
                if (!parse_int64(argv[i], v)) {
                    emit_sv(emit, emit_ctx, "err bad integer arg\n");
                    return false;
                }
                args[nargs++] = cameo::embed::Value::from_int(v);
            }
            cameo::embed::Value out[2]{};
            const auto res = vm_->call_export(cameo::embed::StringView(argv[2]),
                cameo::embed::Span<const cameo::embed::Value>(args, nargs), out, step_budget_);
            last_status_ = res.status;
            if (vm_->is_loaded()) vm_->reset_pools_to(post_main_mark_);
            if (res.status != cameo::embed::Status::Ok) {
                emit_sv(emit, emit_ctx, "err run=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(res.status));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok");
            if (res.result_count > 0 && out[0].is_int()) {
                emit_sv(emit, emit_ctx, " ret=");
                emit_i64(emit, emit_ctx, out[0].as_int());
            }
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "status")) {
            emit_sv(emit, emit_ctx, vm_->is_loaded() ? "loaded=1 crc=" : "loaded=0 crc=");
            emit_hex32(emit, emit_ctx, vm_->image_crc32c());
            emit_sv(emit, emit_ctx, " staged=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(rx_.bytes_received()));
            emit_sv(emit, emit_ctx, " status=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(last_status_));
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        emit_sv(emit, emit_ctx, "err unknown subcommand\n");
        return false;
    }
};
#endif // RTOS_MONITOR

} // namespace cameo::nexrt
