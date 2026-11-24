/** @file examples/bench_demo.cpp
 * @brief Diagnostic RV32IM reference firmware demonstrating the modular
 *        NexRT Monitor (RTOS_MONITOR), fuel-bounded Diagnostic Scripting VM
 *        (RTOS_SCRIPT), static-arena ELF32 Loader (RTOS_ELF_LOADER), AMP
 *        mailbox (RTOS_AMP), and TraceRing — all default OFF and developer opt-in.
 */
#include "rtos.hpp"
#include "rtos_amp.hpp"
#include "rtos_boot.hpp"
#include "rtos_clock.hpp"
#include "rtos_elf_loader.hpp"
#include "rtos_monitor.hpp"
#include "rtos_script.hpp"
#include "rtos_trace.hpp"

namespace {

rtos::monitor::Session mon_session{};
rtos::script::Vm script_vm{};
rtos::elf::Loader elf_loader{};
rtos::amp::AmpChannel<uint32_t, 8> amp_chan{};
rtos::trace::TraceRing<16> flight_trace{};
rtos::EpochClock epoch_clock{};
alignas(16) uint8_t elf_arena[256]{};
uint8_t script_pkg[64]{};

rtos::Thread t_mon;
rtos::Stack<1536> s_mon;
volatile uint32_t bench_tx_bytes{};

size_t sink_write(const char*, size_t len, void*) {
    bench_tx_bytes = bench_tx_bytes + static_cast<uint32_t>(len);
    return len;
}

rtos::SystemInfo bench_snapshot(rtos::ThreadInfo* out, size_t cap, void*) {
    return rtos::snapshot(out, cap);
}

bool bench_time(rtos::monitor::TimeSnapshot& out, void*) {
    const auto tk = rtos::ticks();
    const uint64_t ns = epoch_clock.epoch_ns(tk);
    out.ticks = tk;
    out.clock_hz = 50000000u;
    out.epoch_seconds = ns / 1000000000ull;
    out.epoch_nanoseconds = static_cast<uint32_t>(ns % 1000000000ull);
    out.epoch_synchronized = epoch_clock.synced();
    return true;
}

bool bench_script(int, const char* const*, rtos::monitor::WriteFn, void*, void*) {
    const uint32_t code[] = {
        rtos::script::encode_imm(rtos::script::op_movi, 0, 0, 42),
        rtos::script::encode_imm(rtos::script::op_halt, 0, 0, 0),
    };
    const size_t n = rtos::script::build_package(code, 2, 32, 0, script_pkg, sizeof(script_pkg));
    rtos::script::HostPolicy pol{};
    const auto r = script_vm.execute(script_pkg, n, pol);
    flight_trace.record(rtos::ticks(), rtos::trace::ev_script_run, 0u, static_cast<uint32_t>(r.return_value));
    return r.status == rtos::script::Status::halted;
}

void noop_icache(uintptr_t, size_t, void*) {}

void monitor_task(void*) {
    (void)mon_session.execute_line("help");
    (void)mon_session.execute_line("sched");
    (void)mon_session.execute_line("time");
    (void)mon_session.execute_line("script");
    // Exercise loader fail-closed validation on empty buffer to verify link & zero-heap contract.
    rtos::elf::LoadConfig ecfg{};
    ecfg.sync_icache = noop_icache;
    rtos::elf::LoadedModule mod{};
    (void)elf_loader.load(script_pkg, 16, elf_arena, sizeof(elf_arena), ecfg, mod);
    (void)amp_chan.send(1u);
    for (;;) {
        (void)mon_session.poll_jobs();
        (void)rtos::sleep_for(20u);
    }
}

} // namespace

int main() {
    (void)epoch_clock.initialize(RTOS_TICK_HZ);

    rtos::monitor::Hooks hooks{};
    hooks.snapshot_threads = bench_snapshot;
    hooks.inspect_time = bench_time;
    hooks.run_script = bench_script;
    mon_session.initialize(sink_write, nullptr, hooks);

    (void)rtos::create(t_mon, monitor_task, nullptr, 2, s_mon.words, sizeof(s_mon), "monitor");
    rtos::start(50000000u, RTOS_TICK_HZ);
}
