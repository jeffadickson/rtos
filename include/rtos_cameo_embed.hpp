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

#if RTOS_DMA
#include "rtos_dma.hpp"
#endif

#if RTOS_STORAGE
#include "rtos_storage.hpp"
#endif

#if RTOS_HW_SCHED
#include "rtos_hw_sched.hpp"
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

/// ARM MPS2 AN385 CMSDK APB Timer0 32-bit MMIO window (0x40000000, 4 KB, read-only default).
[[nodiscard]] constexpr cameo::embed::MemoryWindow mps2_cmsdk_timer0(
    uint64_t base = 0x40000000ULL, bool allow_write = false) noexcept {
    return {
        base, 0x1000u,
        allow_write ? cameo::embed::MemoryPerms::kRwAxi : cameo::embed::MemoryPerms::kRoAxi,
        4u, 4u, true
    };
}

/// ARM MPS2 AN385 CMSDK APB UART0 32-bit MMIO window (0x40004000, 4 KB, read-only default).
[[nodiscard]] constexpr cameo::embed::MemoryWindow mps2_cmsdk_uart0(
    uint64_t base = 0x40004000ULL, bool allow_write = false) noexcept {
    return {
        base, 0x1000u,
        allow_write ? cameo::embed::MemoryPerms::kRwAxi : cameo::embed::MemoryPerms::kRoAxi,
        4u, 4u, true
    };
}

/// NexRT AXI4-Lite multi-flash subsystem window (default base 0x10020000, 16 KiB).
[[nodiscard]] constexpr cameo::embed::MemoryWindow nexrt_flash_subsys(
    uint64_t base = 0x10020000ULL, bool allow_write = true) noexcept {
    return {
        base, 0x4000u,
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
    using DmaInspectFn = bool (*)(void* ctx, int64_t& state_out, int64_t& transferred_out,
                                  int64_t& capacity_out, int64_t& generation_out) noexcept;
    using HwSchedReadFn = uint32_t (*)(void* ctx, uint32_t reg_offset) noexcept;

    static constexpr size_t kMaxQueues = 8u;
    static constexpr size_t kMaxDmaChannels = 4u;
    static constexpr size_t kMaxNotifyTargets = 8u;
    static constexpr size_t kMaxSnapshotThreads = 16u;

    struct QueueBinding {
        void* ctx{nullptr};
        QueueSendFn send{nullptr};
        QueueRecvFn recv{nullptr};
        QueueDepthFn depth{nullptr};
    };

    struct DmaBinding {
        void* ctx{nullptr};
        DmaInspectFn inspect{nullptr};
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

    void* hwsched_ctx{nullptr};
    HwSchedReadFn hwsched_read{nullptr};

    QueueBinding queues[kMaxQueues]{};
    DmaBinding dma_channels[kMaxDmaChannels]{};
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

    /// Bind a custom HWSched register-reader callback (e.g., for host tests or MMIO driver).
    void bind_hwsched_reader(HwSchedReadFn fn, void* ctx = nullptr) noexcept {
        hwsched_read = fn;
        hwsched_ctx = ctx;
    }

    /// Bind a custom DMA snapshot provider to channel 0..kMaxDmaChannels-1.
    [[nodiscard]] bool bind_dma_inspector(size_t ch, DmaInspectFn fn, void* ctx = nullptr) noexcept {
        if (ch >= kMaxDmaChannels) return false;
        dma_channels[ch] = {ctx, fn};
        return true;
    }

#if RTOS_DMA
    /// Bind a NexRT rtos::dma::Channel for read-only script inspection (rtos.dma_*).
    [[nodiscard]] bool bind_dma_channel(size_t ch, rtos::dma::Channel& channel) noexcept {
        if (ch >= kMaxDmaChannels) return false;
        dma_channels[ch].ctx = &channel;
        dma_channels[ch].inspect = [](void* c, int64_t& st, int64_t& xfer, int64_t& cap, int64_t& gen) noexcept -> bool {
            rtos::dma::Snapshot snap{};
            if (static_cast<rtos::dma::Channel*>(c)->snapshot(snap) != rtos::dma::Status::ok) return false;
            st = static_cast<int64_t>(snap.state);
            xfer = static_cast<int64_t>(snap.transferred);
            cap = static_cast<int64_t>(snap.capacity);
            gen = static_cast<int64_t>(snap.ticket.generation);
            return true;
        };
        return true;
    }
#endif

#if RTOS_HW_SCHED
    /// Bind a NexRT-HWSched MmioDriver for read-only script inspection (rtos.hwsched_*).
    void bind_hwsched(const rtos::hw_sched::MmioDriver& drv) noexcept {
        hwsched_ctx = const_cast<rtos::hw_sched::MmioDriver*>(&drv);
        hwsched_read = [](void* c, uint32_t off) noexcept -> uint32_t {
            return static_cast<const rtos::hw_sched::MmioDriver*>(c)->read(off);
        };
    }

    /// Bind a NexRT-HWSched Model oracle for host/simulation inspection (rtos.hwsched_*).
    void bind_hwsched_model(const rtos::hw_sched::Model& model) noexcept {
        hwsched_ctx = const_cast<rtos::hw_sched::Model*>(&model);
        hwsched_read = [](void* c, uint32_t off) noexcept -> uint32_t {
            return static_cast<const rtos::hw_sched::Model*>(c)->read(off);
        };
    }
#endif

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
    /// Bind a NexRT TraceRing<Capacity> to dbg.trace(event_id, arg) and watchpoint events.
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

    /// Register all standard rtos.* and dbg.* syscalls, debug writer, and watchpoint trace hook on a StaticVm.
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
        (void)vm.register_host(StandardHostId::DmaState, &cb_dma_state, this);
        (void)vm.register_host(StandardHostId::DmaTransferred, &cb_dma_transferred, this);
        (void)vm.register_host(StandardHostId::DmaCapacity, &cb_dma_capacity, this);
        (void)vm.register_host(StandardHostId::DmaGeneration, &cb_dma_generation, this);
        (void)vm.register_host(StandardHostId::HwSchedStatus, &cb_hwsched_status, this);
        (void)vm.register_host(StandardHostId::HwSchedReadyMask, &cb_hwsched_ready_mask, this);
        (void)vm.register_host(StandardHostId::HwSchedFaultMask, &cb_hwsched_fault_mask, this);
        (void)vm.register_host(StandardHostId::HwSchedSeuCount, &cb_hwsched_seu_count, this);
        (void)vm.register_host(StandardHostId::HwSchedDispatches, &cb_hwsched_dispatches, this);
        vm.set_debug_writer(&cb_debug_write, this);
        vm.set_bus_trace_callback(&cb_bus_trace_event, this);
    }

private:
    static bool cb_debug_write(void* u, const char* data, size_t len) noexcept {
        auto* self = static_cast<HostServices*>(u);
        if (self->log_write == nullptr) return false;
        return self->log_write(self->log_ctx, data, len);
    }

    static void cb_bus_trace_event(void* u, const cameo::embed::BusTraceEvent& ev) noexcept {
        auto* self = static_cast<HostServices*>(u);
        if (self->trace_record == nullptr) return;
        if ((ev.flags & cameo::embed::BusTraceFlags::kWatchpointHit) != 0u) {
            // Event ID 0x0101u = Cameo watchpoint hit (source_id = watchpoint slot, arg = low32 addr)
            (void)self->trace_record(self->trace_ctx, static_cast<uint64_t>(rtos::ticks()),
                                     0x0101u, ev.watch_id, ev.addr_lo);
        }
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

    static bool query_dma(HostServices* self, cameo::embed::HostCallContext& ctx,
                          int64_t& st, int64_t& xfer, int64_t& cap, int64_t& gen) noexcept {
        int64_t ch = 0;
        if (!ctx.try_int(0, ch) || ch < 0 || static_cast<size_t>(ch) >= kMaxDmaChannels) return false;
        const auto& b = self->dma_channels[static_cast<size_t>(ch)];
        if (b.inspect == nullptr) return false;
        return b.inspect(b.ctx, st, xfer, cap, gen);
    }

    static cameo::embed::Status cb_dma_state(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        int64_t st = -1, xfer = 0, cap = 0, gen = 0;
        (void)query_dma(static_cast<HostServices*>(u), ctx, st, xfer, cap, gen);
        (void)ctx.push_result(cameo::embed::Value::from_int(st));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_dma_transferred(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        int64_t st = 0, xfer = 0, cap = 0, gen = 0;
        (void)query_dma(static_cast<HostServices*>(u), ctx, st, xfer, cap, gen);
        (void)ctx.push_result(cameo::embed::Value::from_int(xfer));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_dma_capacity(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        int64_t st = 0, xfer = 0, cap = 0, gen = 0;
        (void)query_dma(static_cast<HostServices*>(u), ctx, st, xfer, cap, gen);
        (void)ctx.push_result(cameo::embed::Value::from_int(cap));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_dma_generation(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        int64_t st = 0, xfer = 0, cap = 0, gen = 0;
        (void)query_dma(static_cast<HostServices*>(u), ctx, st, xfer, cap, gen);
        (void)ctx.push_result(cameo::embed::Value::from_int(gen));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_hwsched_status(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        const uint32_t v = (self->hwsched_read != nullptr) ? self->hwsched_read(self->hwsched_ctx, 0x004u) : 0u;
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(v)));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_hwsched_ready_mask(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        const uint32_t v = (self->hwsched_read != nullptr) ? self->hwsched_read(self->hwsched_ctx, 0x014u) : 0u;
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(v)));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_hwsched_fault_mask(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        const uint32_t v = (self->hwsched_read != nullptr) ? self->hwsched_read(self->hwsched_ctx, 0x018u) : 0u;
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(v)));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_hwsched_seu_count(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        const uint32_t v = (self->hwsched_read != nullptr) ? self->hwsched_read(self->hwsched_ctx, 0x01Cu) : 0u;
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(v)));
        return cameo::embed::Status::Ok;
    }

    static cameo::embed::Status cb_hwsched_dispatches(void* u, cameo::embed::HostCallContext& ctx) noexcept {
        auto* self = static_cast<HostServices*>(u);
        int64_t tid = 0;
        if (!ctx.try_int(0, tid)) return cameo::embed::Status::TypeError;
        uint32_t v = 0u;
        if (self->hwsched_read != nullptr && tid >= 0 && tid < 16) {
            const uint32_t off = 0x200u + static_cast<uint32_t>(tid) * 0x20u + 0x18u;
            v = self->hwsched_read(self->hwsched_ctx, off);
        }
        (void)ctx.push_result(cameo::embed::Value::from_int(static_cast<int64_t>(v)));
        return cameo::embed::Status::Ok;
    }
};

/// Type-erased, zero-heap persistent script slot backend (for JournalFs or board NOR/FRAM).
struct ScriptStorageAdapter {
    void* ctx{nullptr};
    bool (*save)(void* ctx, uint32_t slot, const uint8_t* data, size_t len) noexcept {nullptr};
    bool (*load)(void* ctx, uint32_t slot, uint8_t* out, size_t cap, size_t& out_len) noexcept {nullptr};
    bool (*erase)(void* ctx, uint32_t slot) noexcept {nullptr};
};

#if RTOS_STORAGE
/**
 * @brief Power-fail-atomic, zero-heap persistent Cameo.Embed script slot store over NexRT JournalFs.
 *
 * Each slot uses (ChunksPerSlot + 1) file IDs in JournalFs: chunks are written first,
 * and a 12-byte CRC-32C manifest is written last so interrupted saves never corrupt a prior slot.
 */
template<typename FsType, size_t MaxSlots = 4, size_t ChunksPerSlot = 16>
class JournalFsScriptStore {
    static constexpr uint32_t kManifestMagic = 0x50524353u; // "SCRP" LE
    struct Manifest {
        uint32_t magic{};
        uint32_t total_len{};
        uint32_t crc32c{};
    };
    static_assert(sizeof(Manifest) == 12u);

    FsType* fs_{nullptr};
    uint32_t base_file_id_{0u};

    [[nodiscard]] uint32_t slot_meta_id(uint32_t slot) const noexcept {
        return base_file_id_ + slot * static_cast<uint32_t>(ChunksPerSlot + 1u);
    }

public:
    constexpr JournalFsScriptStore() noexcept = default;

    void bind(FsType& fs, uint32_t base_file_id = 0u) noexcept {
        fs_ = &fs;
        base_file_id_ = base_file_id;
    }

    [[nodiscard]] bool save(uint32_t slot, const uint8_t* data, size_t len) noexcept {
        if (fs_ == nullptr || slot >= MaxSlots || data == nullptr || len == 0u) return false;
        const size_t chunk_cap = FsType::max_file_bytes();
        if (chunk_cap < sizeof(Manifest) || len > chunk_cap * ChunksPerSlot) return false;

        const uint32_t meta = slot_meta_id(slot);
        size_t off = 0u;
        size_t chunk_idx = 0u;
        while (off < len) {
            const size_t take = (len - off < chunk_cap) ? (len - off) : chunk_cap;
            if (fs_->write_file(meta + 1u + static_cast<uint32_t>(chunk_idx), data + off, take) !=
                rtos::storage::FsResult::ok) {
                return false;
            }
            off += take;
            ++chunk_idx;
        }
        const Manifest m{
            kManifestMagic,
            static_cast<uint32_t>(len),
            cameo::embed::crc32c(data, len),
        };
        return fs_->write_file(meta, &m, sizeof(m)) == rtos::storage::FsResult::ok;
    }

    [[nodiscard]] bool load(uint32_t slot, uint8_t* out, size_t cap, size_t& out_len) noexcept {
        out_len = 0u;
        if (fs_ == nullptr || slot >= MaxSlots || out == nullptr) return false;
        const size_t chunk_cap = FsType::max_file_bytes();
        const uint32_t meta = slot_meta_id(slot);

        Manifest m{};
        size_t m_len = 0u;
        if (fs_->read_file(meta, &m, sizeof(m), m_len) != rtos::storage::FsResult::ok) return false;
        if (m_len != sizeof(m) || m.magic != kManifestMagic || m.total_len == 0u || m.total_len > cap) return false;

        size_t off = 0u;
        size_t chunk_idx = 0u;
        while (off < m.total_len) {
            if (chunk_idx >= ChunksPerSlot) return false;
            size_t got = 0u;
            const size_t avail = m.total_len - off;
            const size_t expected = (avail < chunk_cap) ? avail : chunk_cap;
            if (fs_->read_file(meta + 1u + static_cast<uint32_t>(chunk_idx), out + off, expected, got) !=
                rtos::storage::FsResult::ok) {
                return false;
            }
            if (got != expected) return false;
            off += got;
            ++chunk_idx;
        }
        if (cameo::embed::crc32c(out, m.total_len) != m.crc32c) return false;
        out_len = m.total_len;
        return true;
    }

    [[nodiscard]] bool erase(uint32_t slot) noexcept {
        if (fs_ == nullptr || slot >= MaxSlots) return false;
        const uint32_t meta = slot_meta_id(slot);
        const auto mr = fs_->delete_file(meta);
        for (size_t i = 0; i < ChunksPerSlot; ++i) {
            (void)fs_->delete_file(meta + 1u + static_cast<uint32_t>(i));
        }
        return mr == rtos::storage::FsResult::ok || mr == rtos::storage::FsResult::not_found;
    }

    [[nodiscard]] ScriptStorageAdapter adapter() noexcept {
        return {
            this,
            [](void* c, uint32_t slot, const uint8_t* data, size_t len) noexcept -> bool {
                return static_cast<JournalFsScriptStore*>(c)->save(slot, data, len);
            },
            [](void* c, uint32_t slot, uint8_t* out, size_t cap, size_t& out_len) noexcept -> bool {
                return static_cast<JournalFsScriptStore*>(c)->load(slot, out, cap, out_len);
            },
            [](void* c, uint32_t slot) noexcept -> bool {
                return static_cast<JournalFsScriptStore*>(c)->erase(slot);
            },
        };
    }
};
#endif // RTOS_STORAGE

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
 *        Cameo.Embed's PackageReceiver, StaticVm, optional flash slots, and debugger.
 *
 * Supported monitor subcommands:
 *   script reset                   - clear hot-load staging buffer
 *   script hex <HEX...>            - append hex bytes to staging buffer
 *   script load                    - verify CRC-32C, load CEMB package, run chunk main()
 *   script run <export> [args...]  - invoke exported debug function with optional integer args
 *   script status                  - print loaded state, CRC-32C, staged bytes, paused state, last status
 *   script save <slot>             - persist staged verified CEMB image into flash slot
 *   script boot <slot>             - load verified CEMB image from flash slot and run main()
 *   script erase <slot>            - erase flash script slot
 *   script break <proto> <pc>      - set on-target PC breakpoint
 *   script clearbp                 - clear all PC breakpoints
 *   script start <export> [args..] - prepare export paused at PC 0 for single-stepping
 *   script step                    - execute 1 VM instruction
 *   script cont                    - resume until return, breakpoint, or watchpoint
 *   script regs                    - dump active frame (proto, pc, depth) and register values
 *   script bustrace [clear]        - dump captured AXI BusTraceEvent records (or clear ring)
 */
template<typename Vm, size_t MaxPackageBytes = 4096>
class MonitorBridge {
    Vm* vm_{nullptr};
    cameo::embed::PackageReceiver<MaxPackageBytes> rx_{};
    cameo::embed::PoolWatermark post_main_mark_{};
    ScriptStorageAdapter storage_{};
    uint32_t step_budget_{0u};
    cameo::embed::Status last_status_{cameo::embed::Status::Ok};
    bool has_mark_{false};

    static void emit_sv(rtos::monitor::WriteFn emit, void* ctx, cameo::embed::StringView sv) noexcept {
        if (emit != nullptr) (void)emit(sv.data(), sv.size(), ctx);
    }

    static bool parse_int64(const char* s, int64_t& out) noexcept {
        if (s == nullptr || *s == '\0') return false;
        bool neg = false;
        if (*s == '-') { neg = true; ++s; }
        if (*s == '\0') return false;
        uint64_t v = 0;
        const bool is_hex = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'));
        if (is_hex) {
            s += 2;
            if (*s == '\0') return false;
            while (*s != '\0') {
                const int n = cameo::embed::detail::hex_nibble(*s++);
                if (n < 0) return false;
                if (v > (UINT64_MAX >> 4u)) return false;
                v = (v << 4u) | static_cast<uint64_t>(n);
            }
            if (neg && v > 0x8000000000000000ULL) return false;
        } else {
            const uint64_t limit = neg ? 0x8000000000000000ULL : 0x7FFFFFFFFFFFFFFFULL;
            while (*s != '\0') {
                if (*s < '0' || *s > '9') return false;
                const uint64_t d = static_cast<uint64_t>(*s++ - '0');
                if (v > (limit - d) / 10u) return false;
                v = v * 10u + d;
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

    void emit_exec_outcome(const cameo::embed::ExecResult& res,
                           const cameo::embed::Value* out,
                           rtos::monitor::WriteFn emit, void* emit_ctx) noexcept {
        last_status_ = res.status;
        if (res.status == cameo::embed::Status::Breakpoint) {
            emit_sv(emit, emit_ctx, "ok bp proto=");
            emit_i64(emit, emit_ctx, res.fault_proto);
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, res.fault_pc);
            emit_sv(emit, emit_ctx, "\n");
            return;
        }
        if (res.status == cameo::embed::Status::Stepped) {
            emit_sv(emit, emit_ctx, "ok stepped proto=");
            emit_i64(emit, emit_ctx, res.fault_proto);
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, res.fault_pc);
            emit_sv(emit, emit_ctx, "\n");
            return;
        }
        if (res.status == cameo::embed::Status::WatchpointHit) {
            emit_sv(emit, emit_ctx, "ok watch proto=");
            emit_i64(emit, emit_ctx, res.fault_proto);
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, res.fault_pc);
            emit_sv(emit, emit_ctx, " addr=");
            emit_hex32(emit, emit_ctx, static_cast<uint32_t>(vm_->last_watch_addr()));
            emit_sv(emit, emit_ctx, "\n");
            return;
        }
        if (vm_->is_loaded() && !vm_->is_paused() && has_mark_) vm_->reset_pools_to(post_main_mark_);
        if (res.status != cameo::embed::Status::Ok) {
            emit_sv(emit, emit_ctx, "err run=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(res.status));
            emit_sv(emit, emit_ctx, "\n");
            return;
        }
        emit_sv(emit, emit_ctx, "ok");
        if (res.result_count > 0 && out[0].is_int()) {
            emit_sv(emit, emit_ctx, " ret=");
            emit_i64(emit, emit_ctx, out[0].as_int());
        } else if (res.result_count > 0 && out[0].is_ptr()) {
            emit_sv(emit, emit_ctx, " ptr=");
            emit_hex32(emit, emit_ctx, static_cast<uint32_t>(out[0].as_ptr()));
        } else if (res.result_count > 0 && out[0].is_bool()) {
            emit_sv(emit, emit_ctx, out[0].as_bool() ? " ret=1" : " ret=0");
        }
        emit_sv(emit, emit_ctx, "\n");
    }

    cameo::embed::Status load_staged_into_vm() noexcept {
        const auto vst = rx_.verify();
        if (vst != cameo::embed::Status::Ok) { last_status_ = vst; return vst; }
        const auto lst = vm_->load(rx_.span());
        if (lst != cameo::embed::Status::Ok) { last_status_ = lst; return lst; }
        has_mark_ = false;
        const uint32_t budget = step_budget_ > 0u ? step_budget_ : 50000u;
        const auto mres = vm_->run_main(budget);
        last_status_ = mres.status;
        if (mres.status != cameo::embed::Status::Ok) return mres.status;
        post_main_mark_ = vm_->mark_pools();
        has_mark_ = true;
        return cameo::embed::Status::Ok;
    }

public:
    constexpr MonitorBridge() noexcept = default;

    void attach(Vm& vm, uint32_t default_step_budget = 50000u) noexcept {
        vm_ = &vm;
        step_budget_ = default_step_budget;
        has_mark_ = vm.is_loaded();
        post_main_mark_ = has_mark_ ? vm.mark_pools() : cameo::embed::PoolWatermark{};
    }

    void attach_storage(ScriptStorageAdapter adapter) noexcept {
        storage_ = adapter;
    }

    /// Boot a verified script directly from a persistent flash slot into the attached VM.
    [[nodiscard]] cameo::embed::Status boot_from_slot(uint32_t slot) noexcept {
        if (vm_ == nullptr || storage_.load == nullptr) return cameo::embed::Status::NotFound;
        rx_.reset();
        uint8_t tmp[MaxPackageBytes]{};
        size_t len = 0u;
        if (!storage_.load(storage_.ctx, slot, tmp, MaxPackageBytes, len)) {
            last_status_ = cameo::embed::Status::NotFound;
            return last_status_;
        }
        if (!rx_.feed_bytes(tmp, len)) {
            last_status_ = cameo::embed::Status::CapacityExceeded;
            return last_status_;
        }
        return load_staged_into_vm();
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
            emit_sv(emit, emit_ctx,
                "usage: script reset|hex|load|run|status|save|boot|erase|break|clearbp|start|step|cont|regs|bustrace\n");
            return false;
        }
        const cameo::embed::StringView sub(argv[1]);
        const uint32_t budget = step_budget_ > 0u ? step_budget_ : 50000u;

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
            has_mark_ = false;
            const auto mres = vm_->run_main(budget);
            last_status_ = mres.status;
            if (mres.status != cameo::embed::Status::Ok) {
                emit_sv(emit, emit_ctx, "err main=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(mres.status));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            post_main_mark_ = vm_->mark_pools();
            has_mark_ = true;
            emit_sv(emit, emit_ctx, "ok loaded crc=");
            emit_hex32(emit, emit_ctx, vm_->image_crc32c());
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "save")) {
            if (storage_.save == nullptr) { emit_sv(emit, emit_ctx, "err no storage\n"); return false; }
            int64_t slot = 0;
            if (argc < 3 || !parse_int64(argv[2], slot) || slot < 0) {
                emit_sv(emit, emit_ctx, "err bad slot\n");
                return false;
            }
            const auto vst = rx_.verify();
            if (vst != cameo::embed::Status::Ok) {
                last_status_ = vst;
                emit_sv(emit, emit_ctx, "err verify=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(vst));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            const auto sp = rx_.span();
            if (!storage_.save(storage_.ctx, static_cast<uint32_t>(slot), sp.data(), sp.size())) {
                emit_sv(emit, emit_ctx, "err save failed\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok saved slot=");
            emit_i64(emit, emit_ctx, slot);
            emit_sv(emit, emit_ctx, " bytes=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(sp.size()));
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "boot")) {
            if (storage_.load == nullptr) { emit_sv(emit, emit_ctx, "err no storage\n"); return false; }
            int64_t slot = 0;
            if (argc < 3 || !parse_int64(argv[2], slot) || slot < 0) {
                emit_sv(emit, emit_ctx, "err bad slot\n");
                return false;
            }
            const auto bst = boot_from_slot(static_cast<uint32_t>(slot));
            if (bst != cameo::embed::Status::Ok) {
                emit_sv(emit, emit_ctx, "err boot=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(bst));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok booted slot=");
            emit_i64(emit, emit_ctx, slot);
            emit_sv(emit, emit_ctx, " crc=");
            emit_hex32(emit, emit_ctx, vm_->image_crc32c());
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "erase")) {
            if (storage_.erase == nullptr) { emit_sv(emit, emit_ctx, "err no storage\n"); return false; }
            int64_t slot = 0;
            if (argc < 3 || !parse_int64(argv[2], slot) || slot < 0) {
                emit_sv(emit, emit_ctx, "err bad slot\n");
                return false;
            }
            if (!storage_.erase(storage_.ctx, static_cast<uint32_t>(slot))) {
                emit_sv(emit, emit_ctx, "err erase failed\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok erased slot=");
            emit_i64(emit, emit_ctx, slot);
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
                cameo::embed::Span<const cameo::embed::Value>(args, nargs), out, budget);
            emit_exec_outcome(res, out, emit, emit_ctx);
            return res.status == cameo::embed::Status::Ok ||
                   res.status == cameo::embed::Status::Breakpoint ||
                   res.status == cameo::embed::Status::WatchpointHit;
        }
        if (cameo::embed::detail::eq_sv(sub, "break")) {
            int64_t pid = 0, pc = 0;
            if (argc < 4 || !parse_int64(argv[2], pid) || !parse_int64(argv[3], pc) || pid < 0 || pc < 0) {
                emit_sv(emit, emit_ctx, "err usage: script break <proto> <pc>\n");
                return false;
            }
            if (!vm_->set_breakpoint(static_cast<uint16_t>(pid), static_cast<uint32_t>(pc))) {
                emit_sv(emit, emit_ctx, "err bp table full\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok bp set proto=");
            emit_i64(emit, emit_ctx, pid);
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, pc);
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "clearbp")) {
            vm_->clear_breakpoints();
            emit_sv(emit, emit_ctx, "ok clearbp\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "start")) {
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
            const auto st = vm_->start_export(cameo::embed::StringView(argv[2]),
                cameo::embed::Span<const cameo::embed::Value>(args, nargs));
            last_status_ = st;
            if (st != cameo::embed::Status::Ok) {
                emit_sv(emit, emit_ctx, "err start=");
                emit_i64(emit, emit_ctx, static_cast<int64_t>(st));
                emit_sv(emit, emit_ctx, "\n");
                return false;
            }
            emit_sv(emit, emit_ctx, "ok paused proto=");
            emit_i64(emit, emit_ctx, vm_->current_proto());
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, vm_->current_pc());
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "step")) {
            cameo::embed::Value out[2]{};
            const auto res = vm_->step_one(out);
            emit_exec_outcome(res, out, emit, emit_ctx);
            return res.status == cameo::embed::Status::Stepped ||
                   res.status == cameo::embed::Status::Ok ||
                   res.status == cameo::embed::Status::WatchpointHit;
        }
        if (cameo::embed::detail::eq_sv(sub, "cont")) {
            cameo::embed::Value out[2]{};
            const auto res = vm_->continue_run(out, budget);
            emit_exec_outcome(res, out, emit, emit_ctx);
            return res.status == cameo::embed::Status::Ok ||
                   res.status == cameo::embed::Status::Breakpoint ||
                   res.status == cameo::embed::Status::WatchpointHit;
        }
        if (cameo::embed::detail::eq_sv(sub, "regs")) {
            emit_sv(emit, emit_ctx, vm_->is_paused() ? "paused=1 proto=" : "paused=0 proto=");
            emit_i64(emit, emit_ctx, vm_->current_proto());
            emit_sv(emit, emit_ctx, " pc=");
            emit_i64(emit, emit_ctx, vm_->current_pc());
            emit_sv(emit, emit_ctx, " depth=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(vm_->call_depth()));
            if (vm_->is_paused()) {
                const uint16_t max_r = vm_->current_frame_max_stack();
                const uint16_t lim = max_r < 8u ? max_r : 8u;
                for (uint16_t r = 0; r < lim; ++r) {
                    cameo::embed::Value v{};
                    if (!vm_->inspect_reg(r, v) || v.is_nil()) continue;
                    emit_sv(emit, emit_ctx, " r");
                    emit_i64(emit, emit_ctx, r);
                    emit_sv(emit, emit_ctx, "=");
                    if (v.is_int()) emit_i64(emit, emit_ctx, v.as_int());
                    else if (v.is_ptr()) emit_hex32(emit, emit_ctx, static_cast<uint32_t>(v.as_ptr()));
                    else if (v.is_bool()) emit_sv(emit, emit_ctx, v.as_bool() ? "true" : "false");
                    else emit_sv(emit, emit_ctx, "<val>");
                }
            }
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "bustrace")) {
            if (argc >= 3 && argv[2] != nullptr && cameo::embed::detail::eq_sv(cameo::embed::StringView(argv[2]), "clear")) {
                vm_->clear_bus_trace();
                emit_sv(emit, emit_ctx, "ok bustrace cleared\n");
                return true;
            }
            const size_t n = vm_->bus_trace_count();
            for (size_t i = 0; i < n; ++i) {
                cameo::embed::BusTraceEvent ev{};
                if (!vm_->read_bus_trace_oldest(i, ev)) continue;
                emit_sv(emit, emit_ctx, "bt seq=");
                emit_i64(emit, emit_ctx, ev.seq);
                emit_sv(emit, emit_ctx, (ev.flags & cameo::embed::BusTraceFlags::kWrite) ? " wr=1 w=" : " wr=0 w=");
                emit_i64(emit, emit_ctx, ev.width);
                emit_sv(emit, emit_ctx, " addr=");
                emit_hex32(emit, emit_ctx, ev.addr_lo);
                emit_sv(emit, emit_ctx, " val=");
                emit_hex32(emit, emit_ctx, ev.val_lo);
                emit_sv(emit, emit_ctx, (ev.flags & cameo::embed::BusTraceFlags::kWatchpointHit) ? " wp=1\n" : " wp=0\n");
            }
            emit_sv(emit, emit_ctx, "ok bustrace count=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(n));
            emit_sv(emit, emit_ctx, "\n");
            return true;
        }
        if (cameo::embed::detail::eq_sv(sub, "status")) {
            emit_sv(emit, emit_ctx, vm_->is_loaded() ? "loaded=1 crc=" : "loaded=0 crc=");
            emit_hex32(emit, emit_ctx, vm_->image_crc32c());
            emit_sv(emit, emit_ctx, " staged=");
            emit_i64(emit, emit_ctx, static_cast<int64_t>(rx_.bytes_received()));
            emit_sv(emit, emit_ctx, vm_->is_paused() ? " paused=1 status=" : " paused=0 status=");
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
