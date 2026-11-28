/** @file tests/tier2_monitor.cpp
 * @brief Verifies Tier 2 Diagnostics, Binary Flight Recorder, and Modular Bench Monitor:
 *   - Topic<T, MaxSubscribers> static typed pub/sub fan-out
 *   - TraceRing<Capacity> binary flight recorder wrap & serialization
 *   - monitor::Session NXR-038 (line overflow discard + output backpressure)
 *   - monitor::Session NXR-039 (generation-tagged background jobs + cooperative abort)
 *   - monitor::Session NXR-040 (all original NexRT commands: help, sched, cycles, time, io, vault, guard, flightlog, runs, abort, script)
 */
#include "rtos_monitor.hpp"
#include "rtos_topic.hpp"
#include "rtos_trace.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace rtos;

namespace {

struct TelemetrySample {
    uint32_t bus_mv{};
    int16_t temp_dc{};
};

struct SubCapture {
    uint32_t count{};
    uint32_t last_mv{};
};

void on_telem(const TelemetrySample& s, void* ctx) {
    auto* cap = static_cast<SubCapture*>(ctx);
    ++cap->count;
    cap->last_mv = s.bus_mv;
}

void test_topic() {
    Topic<TelemetrySample, 2> bus;
    SubCapture c1{}, c2{};
    size_t s1 = 99, s2 = 99;
    assert(!bus.subscribe(nullptr));
    assert(bus.subscribe(on_telem, &c1, &s1));
    assert(bus.subscribe(on_telem, &c2, &s2));
    assert(!bus.subscribe(on_telem, &c1)); // Full!
    assert(bus.subscriber_count() == 2u);

    assert(bus.publish({3310u, 245}) == 2u);
    assert(c1.count == 1u && c1.last_mv == 3310u);
    assert(c2.count == 1u && c2.last_mv == 3310u);

    TelemetrySample latest{};
    assert(bus.latest(latest) && latest.bus_mv == 3310u && latest.temp_dc == 245);

    assert(bus.unsubscribe(s1));
    assert(!bus.unsubscribe(s1));
    assert(bus.publish({3290u, 250}) == 1u);
    assert(c1.count == 1u && c2.count == 2u && c2.last_mv == 3290u);
}

void test_trace_ring() {
    trace::TraceRing<8> ring;
    for (uint32_t i = 0; i < 12; ++i) {
        ring.record(1000ull + i * 10ull, trace::ev_dispatch, static_cast<uint16_t>(i & 3u), i);
    }
    assert(ring.count() == 8u);
    assert(ring.overwritten == 4u);

    trace::Event ev{};
    assert(ring.read_oldest(0, ev));
    assert(ev.arg == 4u && ev.timestamp == 1040ull);
    assert(ring.read_oldest(7, ev));
    assert(ev.arg == 11u && ev.timestamp == 1110ull);
    assert(!ring.read_oldest(8, ev));

    uint8_t blob[256]{};
    const size_t bytes = ring.serialize(blob, sizeof(blob));
    assert(bytes == sizeof(trace::DumpHeader) + 8u * sizeof(trace::Event));
    // Verify NXTR magic + version=1 + retained_count=8 + total_recorded=12.
    uint32_t hdr[4]{};
    std::memcpy(hdr, blob, sizeof(hdr));
    assert(hdr[0] == trace::dump_magic);
    assert(hdr[1] == trace::dump_version);
    assert(hdr[2] == 8u);
    assert(hdr[3] == 12u);
}

struct OutSink {
    std::string text;
    size_t max_chunk{4096};
};

size_t write_sink(const char* data, size_t len, void* ctx) {
    auto* sink = static_cast<OutSink*>(ctx);
    const size_t accept = len < sink->max_chunk ? len : sink->max_chunk;
    sink->text.append(data, accept);
    return accept;
}

struct MockBench {
    trace::TraceRing<8> trace_ring;
    uint32_t vault_calls{};
    uint32_t script_calls{};
};

SystemInfo mock_snapshot(ThreadInfo* out, size_t cap, void*) {
    SystemInfo info{};
    info.ticks = 4200;
    info.clock_hz = 50000000;
    info.elapsed_cycles = 210000000ull;
    info.required_threads = 2;
    if (out && cap >= 2) {
        out[0].name = "control";
        out[0].state = State::ready;
        out[0].priority = 5;
        out[0].effective_priority = 5;
        out[0].stack_bytes = 1024;
        out[0].guard_ok = true;
        out[0].current = true;
        out[0].runtime_cycles = 123456;
        out[0].dispatches = 99;

        out[1].name = "idle";
        out[1].state = State::ready;
        out[1].priority = 0;
        out[1].effective_priority = 0;
        out[1].stack_bytes = 512;
        out[1].guard_ok = true;
        out[1].idle = true;
        out[1].runtime_cycles = 900000;
        out[1].dispatches = 99;
        info.written_threads = 2;
    }
    return info;
}

bool mock_time(monitor::TimeSnapshot& out, void*) {
    out.ticks = 4200;
    out.clock_hz = 50000000;
    out.epoch_seconds = 1767225600ull;
    out.epoch_nanoseconds = 500000000u;
    out.epoch_synchronized = true;
    return true;
}

bool mock_io(const char*, monitor::WriteFn emit, void* emit_ctx, void*) {
    const char msg[] = "IO uart0=ok spi_flash0=ready\n";
    emit(msg, sizeof(msg) - 1, emit_ctx);
    return true;
}

bool mock_vault(int argc, const char* const*, monitor::WriteFn emit, void* emit_ctx, void* ctx) {
    auto* b = static_cast<MockBench*>(ctx);
    ++b->vault_calls;
    const char msg[] = "VAULT keys=3 bad_blocks=1\n";
    emit(msg, sizeof(msg) - 1, emit_ctx);
    return argc >= 1;
}

bool mock_guard(monitor::WriteFn emit, void* emit_ctx, void*) {
    const char msg[] = "GUARD slot=flight_a safe_mode=no wdt=healthy\n";
    emit(msg, sizeof(msg) - 1, emit_ctx);
    return true;
}

bool mock_flightlog(const char*, monitor::WriteFn emit, void* emit_ctx, void*) {
    const char msg[] = "FLIGHTLOG retained=8\n";
    emit(msg, sizeof(msg) - 1, emit_ctx);
    return true;
}

bool mock_script(int argc, const char* const*, monitor::WriteFn emit, void* emit_ctx, void* ctx) {
    auto* b = static_cast<MockBench*>(ctx);
    ++b->script_calls;
    const char msg[] = "SCRIPT status=halted ret=3300\n";
    emit(msg, sizeof(msg) - 1, emit_ctx);
    return argc >= 1;
}

struct JobStateCtx {
    uint32_t remaining{5};
    bool saw_cancel{false};
};

bool test_job_step(bool cancel, void* ctx, monitor::WriteFn, void*) {
    auto* j = static_cast<JobStateCtx*>(ctx);
    if (cancel) {
        j->saw_cancel = true;
        return false;
    }
    if (j->remaining > 0) --j->remaining;
    return j->remaining > 0;
}

void test_monitor_commands_and_contracts() {
    MockBench bench;
    OutSink sink;
    monitor::Hooks hooks{};
    hooks.snapshot_threads = mock_snapshot;
    hooks.inspect_time = mock_time;
    hooks.inspect_io = mock_io;
    hooks.inspect_vault = mock_vault;
    hooks.inspect_guard = mock_guard;
    hooks.inspect_flightlog = mock_flightlog;
    hooks.run_script = mock_script;
    hooks.context = &bench;

    monitor::Session mon;
    mon.initialize(write_sink, &sink, hooks);

    // NXR-040: Test every original NexRT command name.
    assert(mon.execute_line("help"));
    assert(sink.text.find("sched cycles time io vault guard flightlog runs abort script") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("sched"));
    assert(sink.text.find("SCHED ticks=4200 threads=2/2") != std::string::npos);
    assert(sink.text.find("control state=ready prio=5/5") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("cycles"));
    assert(sink.text.find("CYCLES elapsed=210000000") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("time"));
    assert(sink.text.find("TIME ticks=4200 hz=50000000 epoch=1767225600.500000000 sync=yes") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("io"));
    assert(sink.text.find("IO uart0=ok") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("vault status"));
    assert(bench.vault_calls == 1u && sink.text.find("VAULT keys=3") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("guard"));
    assert(sink.text.find("GUARD slot=flight_a") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("flightlog"));
    assert(sink.text.find("FLIGHTLOG retained=8") != std::string::npos);

    sink.text.clear();
    assert(mon.execute_line("script bus_health"));
    assert(bench.script_calls == 1u && sink.text.find("SCRIPT status=halted") != std::string::npos);

    assert(!mon.execute_line("unknown_cmd"));

    // NXR-038: Line overflow must discard until newline, emit ONE error, and keep session clean.
    sink.text.clear();
    std::string huge(220, 'A');
    huge.push_back('\n');
    mon.feed_bytes(huge.data(), huge.size());
    assert(mon.line_overflow_count() == 1u);
    assert(sink.text == "ERR line overflow\n");

    // Next normal command via streaming feed_bytes works cleanly (with backspace support!).
    sink.text.clear();
    const char cmd_stream[] = "helx\bp\n";
    mon.feed_bytes(cmd_stream, sizeof(cmd_stream) - 1);
    assert(sink.text.find("NexRT Monitor commands:") != std::string::npos);

    // NXR-038: Output backpressure tracking.
    sink.max_chunk = 4;
    const uint32_t before_drop = mon.dropped_output_bytes();
    assert(mon.execute_line("help"));
    assert(mon.dropped_output_bytes() > before_drop);
    sink.max_chunk = 4096;

    // NXR-039: Generation-tagged background jobs & cooperative abort + stale-ID rejection.
    JobStateCtx j1{}, j2{};
    const uint32_t id1 = mon.spawn_job("scrub", test_job_step, &j1);
    assert(id1 != 0u);
    assert(mon.poll_jobs() == 1u);
    assert(j1.remaining == 4u);

    sink.text.clear();
    assert(mon.execute_line("runs"));
    assert(sink.text.find("name=scrub state=running") != std::string::npos);

    // Abort via monitor command.
    char abort_cmd[64]{};
    std::snprintf(abort_cmd, sizeof(abort_cmd), "abort 0x%x", id1);
    sink.text.clear();
    assert(mon.execute_line(abort_cmd));
    assert(sink.text.find("OK abort requested") != std::string::npos);
    assert(mon.poll_jobs() == 1u);
    assert(j1.saw_cancel);

    // Reuse slot 0 with a new job -> receives a NEW generation; stale id1 must be rejected!
    const uint32_t id2 = mon.spawn_job("telemetry", test_job_step, &j2);
    assert(id2 != 0u && (id2 & 0xffu) == (id1 & 0xffu) && id2 != id1);
    assert(!mon.cancel_job(id1)); // Stale generation rejected!
    // Bug #7 regression: corrupt upper 8 bits in job_id must be rejected!
    assert(!mon.cancel_job(id2 | 0xFF000000u));
    assert(!mon.cancel_job(id2 | 0x01000000u));
    assert(mon.cancel_job(id2));
    assert(mon.poll_jobs() == 1u);
    assert(j2.saw_cancel);

    // Bug #7 regression: commands with > max_tokens (8) must be rejected instead of silently truncated!
    sink.text.clear();
    assert(!mon.execute_line("vault a b c d e f g h"));
    assert(sink.text == "ERR too many arguments\n");
    // Exactly 8 tokens is still accepted.
    sink.text.clear();
    assert(mon.execute_line("vault a b c d e f g"));
    assert(sink.text.find("VAULT keys=3") != std::string::npos);

    // Abort argument validation & CRLF streaming + empty line handling.
    sink.text.clear();
    assert(mon.execute_line("abort"));
    assert(sink.text == "ERR usage: abort <job_id>\n");
    sink.text.clear();
    assert(mon.execute_line("abort not_a_num"));
    assert(sink.text == "ERR invalid job id\n");
    sink.text.clear();
    assert(mon.execute_line("abort 0xdeadbeef"));
    assert(sink.text == "ERR job not active or stale generation\n");
    sink.text.clear();
    assert(mon.execute_line("   \t  "));
    assert(sink.text.empty());
    const char crlf_stream[] = "help\r\n";
    mon.feed_bytes(crlf_stream, sizeof(crlf_stream) - 1);
    assert(sink.text.find("NexRT Monitor commands:") != std::string::npos);

    // Saturate all max_jobs slots and verify 5th spawn returns 0; verify natural completion!
    JobStateCtx slots[5]{};
    uint32_t ids[4]{};
    for (size_t i = 0; i < 4; ++i) {
        slots[i].remaining = 1;
        ids[i] = mon.spawn_job("batch", test_job_step, &slots[i]);
        assert(ids[i] != 0u);
    }
    assert(mon.spawn_job("overflow", test_job_step, &slots[4]) == 0u);
    assert(mon.spawn_job("nullfn", nullptr, nullptr) == 0u);
    assert(mon.poll_jobs() == 4u); // All 4 complete naturally
    assert(mon.poll_jobs() == 0u); // None active anymore
    sink.text.clear();
    assert(mon.execute_line("runs"));
    assert(sink.text.find("state=done") != std::string::npos);

    // Null-hook fallback paths for every command.
    monitor::Session bare;
    OutSink bare_sink;
    bare.initialize(write_sink, &bare_sink, monitor::Hooks{});
    const char* cmds[] = {"sched", "cycles", "time", "io", "vault", "guard", "flightlog", "script"};
    for (const char* c : cmds) {
        bare_sink.text.clear();
        assert(bare.execute_line(c));
        assert(bare_sink.text.find("ERR ") == 0u && bare_sink.text.find("unavailable") != std::string::npos);
    }
    assert(!bare.execute_line(nullptr));
    std::string direct_overflow(200, 'X');
    assert(!bare.execute_line(direct_overflow.c_str()));
}

} // namespace

int main() {
    test_topic();
    test_trace_ring();
    test_monitor_commands_and_contracts();
    std::printf("PASS: Tier 2 diagnostics, Topic, TraceRing, and NexRT Monitor (NXR-038..040).\n");
    return 0;
}
