/** @file
 * @brief Flagship StateForge + Telemetry + Watchdog + Hardware-Scheduler Demo.
 *
 * Boot-static, zero-heap, zero-constructor RV32IM application exercising Phases 1-5
 * AND compile-time concurrency safety (include/rtos_safe.hpp):
 *   - StaticTask<StackBytes, Priority>: non-copyable thread + aligned stack bundles
 *   - TypedChannel<T, N>: compile-time separation of ThreadSender, ThreadReceiver, IsrSender
 *   - CeilingResource<T, 4>: Immediate Priority Ceiling Protocol (IPCP) for actuator bus
 *   - OrderedLock<T, 1> -> OrderedLock<T, 2>: compile-time deadlock-free lock hierarchy
 *   - Per-thread WCET execution budget watchdog (RTOS_BUDGET_WATCHDOG)
 *   - Seamless compilation for SW scheduler, NexRT-HWSched (Phase 3), and RISCue-MT Banked RF (Phase 4)
 */
#include "rtos_safe.hpp"
#include "rtos_web.hpp"
#if RTOS_HW_SCHED
#include "rtos_hw_sched.hpp"
#endif

namespace {

enum class ForgeState : uint8_t { boot_check, nominal, degraded, safe_hold };

struct SensorSample {
    uint32_t sequence;
    uint32_t reading;
    uint32_t timestamp_ticks;
};

struct ActuatorCommand {
    uint32_t sequence;
    ForgeState mode;
    uint16_t setpoint;
};

struct ActuatorBusState {
    uint32_t completed_cycles{};
    uint16_t last_setpoint{};
    ForgeState last_mode{ForgeState::boot_check};
};

struct ForgeTelemetry {
    uint32_t samples_processed{};
    uint32_t mode_transitions{};
};

struct FaultLedger {
    uint32_t budget_faults{};
    uint32_t irq_drops{};
};

constexpr uintptr_t kUartBase = 0x10000000u;
constexpr uint32_t kEvtTelemetryDue = 1u << 0;

// Non-copyable static thread + aligned stack bundles (zero stack/TCB mismatch risk).
static rtos::safe::StaticTask<1024, 4> sensor_task;
static rtos::safe::StaticTask<1024, 3> engine_task;
static rtos::safe::StaticTask<1024, 3> actuator_task;
static rtos::safe::StaticTask<1024, 1> telemetry_task;

// Capability-separated typed channels: ISR only gets IsrSender (blocking send cannot compile).
static rtos::safe::TypedChannel<uint32_t, 8> irq_ticks_channel;
static rtos::safe::TypedChannel<SensorSample, 8> sensor_channel;
static rtos::safe::TypedChannel<ActuatorCommand, 8> command_channel;

// IPCP ceiling resource (ceiling = 4 >= every accessing thread's base priority).
static rtos::safe::CeilingResource<ActuatorBusState, 4> actuator_bus;

// Compile-time ascending lock hierarchy (Level 1 -> Level 2; reverse order fails to compile).
static rtos::safe::OrderedLock<ForgeTelemetry, 1> telemetry_state;
static rtos::safe::OrderedLock<FaultLedger, 2> fault_ledger;

static rtos::EventFlags forge_events;
static rtos::Logger<8, 96> console;
static rtos::AtomicU32 irq_pulses, watchdog_faults;

int uart_write(void*, const char* bytes, size_t length) {
    auto& status = *reinterpret_cast<volatile uint32_t*>(kUartBase + 8u);
    auto& transmit = *reinterpret_cast<volatile uint32_t*>(kUartBase + 4u);
    size_t sent = 0;
    while (sent < length && !(status & 8u)) {
        transmit = static_cast<unsigned char>(bytes[sent++]);
    }
    return static_cast<int>(sent);
}

void external_irq_isr(void*) {
    const uint32_t pulse = irq_pulses.fetch_add(1u) + 1u;
    auto isr_tx = irq_ticks_channel.isr_sender();
    (void)isr_tx.try_send(pulse);
}

void on_budget_fault(const rtos::Thread&, uint64_t) {
    watchdog_faults.fetch_add(1u);
}

void sensor_ingest(void*) {
    auto irq_rx = irq_ticks_channel.thread_receiver();
    auto sample_tx = sensor_channel.thread_sender();
    uint32_t sequence = 0;
    uint32_t lfsr = 0x13572468u;
    for (;;) {
        uint32_t pulse = 0;
        (void)irq_rx.receive(pulse, 5);
        lfsr = lfsr * 1664525u + 1013904223u + pulse;
        const SensorSample sample{++sequence, lfsr & 0x0FFFu, rtos::ticks()};
        (void)sample_tx.send(sample, 10);
        if ((sequence & 7u) == 0u) {
            (void)forge_events.set(kEvtTelemetryDue);
        }
    }
}

void state_engine(void*) {
    auto sample_rx = sensor_channel.thread_receiver();
    auto cmd_tx = command_channel.thread_sender();
    ForgeState mode = ForgeState::boot_check;
    uint32_t healthy_streak = 0;
    for (;;) {
        SensorSample sample{};
        if (sample_rx.receive(sample, 20) != rtos::Result::ok) {
            mode = ForgeState::safe_hold;
            continue;
        }
        const ForgeState previous = mode;
        if (mode == ForgeState::boot_check) {
            if (++healthy_streak >= 2u) mode = ForgeState::nominal;
        } else if (sample.reading > 3900u) {
            mode = ForgeState::degraded;
        } else if (mode == ForgeState::degraded && sample.reading < 3000u) {
            mode = ForgeState::nominal;
        }
        // Strict compile-time lock hierarchy: RootLockToken (0) -> Level 1 -> Level 2.
        (void)telemetry_state.with_lock(rtos::safe::RootLockToken{}, 10, [&](ForgeTelemetry& tel, auto tok1) {
            ++tel.samples_processed;
            if (mode != previous) ++tel.mode_transitions;
            (void)fault_ledger.with_lock(tok1, 10, [&](FaultLedger& ledger, auto) {
                ledger.budget_faults = watchdog_faults.load();
            });
        });
        const uint16_t setpoint = (mode == ForgeState::nominal) ? static_cast<uint16_t>(sample.reading >> 2)
                                                                : static_cast<uint16_t>(256u);
        const ActuatorCommand cmd{sample.sequence, mode, setpoint};
        (void)cmd_tx.send(cmd, 10);
    }
}

void actuator_cmd(void*) {
    auto cmd_rx = command_channel.thread_receiver();
    for (;;) {
        ActuatorCommand cmd{};
        if (cmd_rx.receive(cmd, 25) != rtos::Result::ok) continue;
        // Immediate Priority Ceiling Protocol: zero priority inversion, zero deadlock.
        (void)actuator_bus.access(10, [&](ActuatorBusState& bus) {
            ++bus.completed_cycles;
            bus.last_setpoint = cmd.setpoint;
            bus.last_mode = cmd.mode;
        });
    }
}

void telemetry_loop(void*) {
    uint8_t json_buf[96]{};
    for (;;) {
        uint32_t observed = 0;
        (void)forge_events.wait_any(kEvtTelemetryDue, observed, 20, rtos::EventClear::yes);
        uint32_t cycles = 0;
        uint32_t faults = 0;
        (void)actuator_bus.access(10, [&](ActuatorBusState& bus) {
            cycles = bus.completed_cycles;
        });
        (void)telemetry_state.with_lock(rtos::safe::RootLockToken{}, 10, [&](ForgeTelemetry&, auto tok1) {
            (void)fault_ledger.with_lock(tok1, 10, [&](FaultLedger& ledger, auto) {
                faults = ledger.budget_faults;
            });
        });

        rtos::web::JsonWriter writer({json_buf, sizeof(json_buf)});
        writer.text("{\"cycles\":");
        writer.unsigned_number(cycles);
        writer.text(",\"ticks\":");
        writer.unsigned_number(rtos::ticks());
        writer.text(",\"faults\":");
        writer.unsigned_number(faults);
#if RTOS_HW_SCHED
        constexpr rtos::hw_sched::MmioDriver hw(RTOS_HWSCHED_BASE);
        writer.text(",\"seu\":");
        writer.unsigned_number(hw.seu_count());
        writer.text(",\"hw\":1}\n");
#else
        writer.text(",\"hw\":0}\n");
#endif
        const auto payload = writer.finish();
        if (payload.size > 0) {
            (void)console.write(reinterpret_cast<const char*>(payload.data), payload.size);
            (void)console.drain(uart_write, nullptr, payload.size);
        }
    }
}

} // namespace

extern "C" int main() {
    if (!rtos::register_external_irq(external_irq_isr, nullptr)) return 1;
#if RTOS_BUDGET_WATCHDOG
    if (!rtos::register_budget_fault_hook(on_budget_fault)) return 1;
#endif
    if (!sensor_task.register_task(sensor_ingest, nullptr, "sensor", 250000u) ||
        !engine_task.register_task(state_engine, nullptr, "stateforge", 400000u) ||
        !actuator_task.register_task(actuator_cmd, nullptr, "actuator", 250000u) ||
        !telemetry_task.register_task(telemetry_loop, nullptr, "telemetry", 600000u)) {
        return 1;
    }
    rtos::start(100000000u, 1000u);
}
