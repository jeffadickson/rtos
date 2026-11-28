#include "rtos_monitor.hpp"

namespace rtos::monitor {
namespace {

size_t cstr_len(const char* s) {
    if (!s) return 0;
    size_t n = 0;
    while (s[n] != '\0') ++n;
    return n;
}

bool cstr_eq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool parse_u32(const char* s, uint32_t& out) {
    if (!s || *s == '\0') return false;
    uint32_t base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (*s == '\0') return false;
    }
    uint64_t val = 0;
    while (*s) {
        uint32_t d = 0;
        if (*s >= '0' && *s <= '9') {
            d = static_cast<uint32_t>(*s - '0');
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = static_cast<uint32_t>(10 + (*s - 'a'));
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = static_cast<uint32_t>(10 + (*s - 'A'));
        } else {
            return false;
        }
        val = val * base + d;
        if (val > 0xffffffffULL) return false;
        ++s;
    }
    out = static_cast<uint32_t>(val);
    return true;
}

const char* state_name(State s) {
    switch (s) {
        case State::unused:      return "unused";
        case State::ready:       return "ready";
        case State::sleeping:    return "sleep";
        case State::blocked:     return "block";
        case State::finished:    return "done";
        case State::quarantined: return "quar";
    }
    return "?";
}

const char* job_state_name(JobState s) {
    switch (s) {
        case JobState::free_slot:        return "free";
        case JobState::running:          return "running";
        case JobState::cancel_requested: return "aborting";
        case JobState::completed:        return "done";
        case JobState::canceled:         return "aborted";
    }
    return "?";
}

} // namespace

void Session::initialize(WriteFn writer, void* writer_ctx, const Hooks& hooks) {
    writer_ = writer;
    writer_ctx_ = writer_ctx;
    hooks_ = hooks;
    line_len_ = 0;
    line_overflowed_ = false;
    dropped_output_ = 0;
    overflow_count_ = 0;
    command_count_ = 0;
    next_generation_ = 1;
    for (size_t i = 0; i < max_jobs; ++i) {
        jobs_[i] = JobSlot{};
    }
}

void Session::emit_raw(const char* data, size_t len) {
    if (!writer_ || !data || len == 0) return;
    const size_t accepted = writer_(data, len, writer_ctx_);
    if (accepted < len) {
        dropped_output_ += static_cast<uint32_t>(len - accepted);
    }
}

void Session::emit_str(const char* s) {
    if (!s) return;
    emit_raw(s, cstr_len(s));
}

void Session::emit_line(const char* s) {
    emit_str(s);
    emit_raw("\n", 1);
}

void Session::emit_u64(uint64_t v) {
    char buf[24]{};
    size_t pos = 0;
    if (v == 0) {
        buf[pos++] = '0';
    } else {
        while (v > 0 && pos < sizeof(buf)) {
            buf[pos++] = static_cast<char>('0' + (v % 10u));
            v /= 10u;
        }
    }
    char rev[24]{};
    for (size_t i = 0; i < pos; ++i) {
        rev[i] = buf[pos - 1u - i];
    }
    emit_raw(rev, pos);
}

void Session::emit_hex32(uint32_t v) {
    static constexpr char hex[] = "0123456789abcdef";
    char buf[10] = {'0', 'x', '0', '0', '0', '0', '0', '0', '0', '0'};
    for (int i = 7; i >= 0; --i) {
        buf[2 + i] = hex[v & 0xfu];
        v >>= 4;
    }
    emit_raw(buf, 10);
}

void Session::feed_bytes(const char* data, size_t len) {
    if (!data) return;
    for (size_t i = 0; i < len; ++i) {
        const char ch = data[i];
        if (ch == '\r' || ch == '\n') {
            if (line_overflowed_) {
                line_overflowed_ = false;
                line_len_ = 0;
                ++overflow_count_;
                emit_line("ERR line overflow");
            } else if (line_len_ > 0) {
                line_buf_[line_len_] = '\0';
                (void)execute_line(line_buf_);
                line_len_ = 0;
            }
            continue;
        }
        if (line_overflowed_) {
            continue; // NXR-038: discard remaining overflow bytes until newline.
        }
        if (ch == '\b' || ch == 0x7f) {
            if (line_len_ > 0) --line_len_;
            continue;
        }
        if (line_len_ < max_line_bytes) {
            line_buf_[line_len_++] = ch;
        } else {
            line_overflowed_ = true;
        }
    }
}

bool Session::execute_line(const char* line) {
    if (!line) return false;
    char scratch[max_line_bytes + 1]{};
    size_t len = 0;
    while (line[len] != '\0') {
        if (len >= max_line_bytes) {
            ++overflow_count_;
            emit_line("ERR line overflow");
            return false;
        }
        scratch[len] = line[len];
        ++len;
    }
    scratch[len] = '\0';

    const char* argv[max_tokens]{};
    int argc = 0;
    size_t pos = 0;
    while (pos < len && argc < static_cast<int>(max_tokens)) {
        while (pos < len && (scratch[pos] == ' ' || scratch[pos] == '\t')) ++pos;
        if (pos >= len) break;
        argv[argc++] = &scratch[pos];
        while (pos < len && scratch[pos] != ' ' && scratch[pos] != '\t') ++pos;
        if (pos < len) scratch[pos++] = '\0';
    }
    while (pos < len && (scratch[pos] == ' ' || scratch[pos] == '\t')) ++pos;
    if (pos < len) {
        emit_line("ERR too many arguments");
        return false;
    }
    if (argc == 0) return true;

    ++command_count_;
    const char* cmd = argv[0];
    if (cstr_eq(cmd, "help")) {
        cmd_help();
        return true;
    }
    if (cstr_eq(cmd, "sched")) {
        cmd_sched();
        return true;
    }
    if (cstr_eq(cmd, "cycles")) {
        cmd_cycles();
        return true;
    }
    if (cstr_eq(cmd, "time")) {
        cmd_time();
        return true;
    }
    if (cstr_eq(cmd, "io")) {
        cmd_io(argc, argv);
        return true;
    }
    if (cstr_eq(cmd, "vault")) {
        cmd_vault(argc, argv);
        return true;
    }
    if (cstr_eq(cmd, "guard")) {
        cmd_guard();
        return true;
    }
    if (cstr_eq(cmd, "flightlog")) {
        cmd_flightlog(argc, argv);
        return true;
    }
    if (cstr_eq(cmd, "runs")) {
        cmd_runs();
        return true;
    }
    if (cstr_eq(cmd, "abort")) {
        cmd_abort(argc, argv);
        return true;
    }
    if (cstr_eq(cmd, "script")) {
        cmd_script(argc, argv);
        return true;
    }
    emit_str("ERR unknown command: ");
    emit_line(cmd);
    return false;
}

uint32_t Session::spawn_job(const char* name, JobStepFn step_fn, void* job_ctx) {
    if (!step_fn) return 0;
    size_t slot = max_jobs;
    for (size_t i = 0; i < max_jobs; ++i) {
        if (jobs_[i].state == JobState::free_slot ||
            jobs_[i].state == JobState::completed ||
            jobs_[i].state == JobState::canceled) {
            slot = i;
            break;
        }
    }
    if (slot == max_jobs) return 0;

    uint16_t gen = next_generation_++;
    if (gen == 0) gen = next_generation_++; // Never emit generation 0.
    auto& job = jobs_[slot];
    job.generation = gen;
    job.state = JobState::running;
    job.step = step_fn;
    job.context = job_ctx;
    job.steps_run = 0;
    size_t k = 0;
    if (name) {
        while (name[k] != '\0' && k + 1 < sizeof(job.name)) {
            job.name[k] = name[k];
            ++k;
        }
    }
    job.name[k] = '\0';
    return (static_cast<uint32_t>(gen) << 8) | static_cast<uint32_t>(slot);
}

bool Session::cancel_job(uint32_t job_id) {
    if ((job_id >> 24) != 0u) return false;
    const size_t slot = static_cast<size_t>(job_id & 0xffu);
    const uint16_t gen = static_cast<uint16_t>((job_id >> 8) & 0xffffu);
    if (slot >= max_jobs || gen == 0) return false;
    auto& job = jobs_[slot];
    if (job.generation != gen) return false;
    if (job.state != JobState::running && job.state != JobState::cancel_requested) return false;
    job.state = JobState::cancel_requested;
    return true;
}

size_t Session::poll_jobs() {
    size_t ran = 0;
    for (size_t i = 0; i < max_jobs; ++i) {
        auto& job = jobs_[i];
        if (job.state == JobState::running || job.state == JobState::cancel_requested) {
            const bool cancel = (job.state == JobState::cancel_requested);
            const bool keep_going = job.step ? job.step(cancel, job.context, writer_, writer_ctx_) : false;
            ++job.steps_run;
            ++ran;
            if (!keep_going) {
                job.state = cancel ? JobState::canceled : JobState::completed;
            }
        }
    }
    return ran;
}

void Session::cmd_help() {
    emit_line("NexRT Monitor commands: help sched cycles time io vault guard flightlog runs abort script");
}

void Session::cmd_sched() {
    if (!hooks_.snapshot_threads) {
        emit_line("ERR sched unavailable");
        return;
    }
    ThreadInfo rows[max_snapshot_threads]{};
    const SystemInfo info = hooks_.snapshot_threads(rows, max_snapshot_threads, hooks_.context);
    emit_str("SCHED ticks=");
    emit_u64(info.ticks);
    emit_str(" threads=");
    emit_u64(info.written_threads);
    emit_str("/");
    emit_u64(info.required_threads);
    emit_raw("\n", 1);
    for (size_t i = 0; i < info.written_threads; ++i) {
        const auto& r = rows[i];
        emit_str("  ");
        emit_str(r.name ? r.name : "unnamed");
        emit_str(" state=");
        emit_str(state_name(r.state));
        emit_str(" prio=");
        emit_u64(r.priority);
        emit_str("/");
        emit_u64(r.effective_priority);
        emit_str(" stack=");
        emit_u64(r.stack_bytes);
        emit_str(r.guard_ok ? " guard=ok" : " guard=FAULT");
        if (r.current) emit_str(" *");
        emit_raw("\n", 1);
    }
}

void Session::cmd_cycles() {
    if (!hooks_.snapshot_threads) {
        emit_line("ERR cycles unavailable");
        return;
    }
    ThreadInfo rows[max_snapshot_threads]{};
    const SystemInfo info = hooks_.snapshot_threads(rows, max_snapshot_threads, hooks_.context);
    emit_str("CYCLES elapsed=");
    emit_u64(info.elapsed_cycles);
    emit_str(" hz=");
    emit_u64(info.clock_hz);
    emit_raw("\n", 1);
    for (size_t i = 0; i < info.written_threads; ++i) {
        emit_str("  ");
        emit_str(rows[i].name ? rows[i].name : "unnamed");
        emit_str(" cycles=");
        emit_u64(rows[i].runtime_cycles);
        emit_str(" dispatches=");
        emit_u64(rows[i].dispatches);
        emit_raw("\n", 1);
    }
}

void Session::cmd_time() {
    if (!hooks_.inspect_time) {
        emit_line("ERR time unavailable");
        return;
    }
    TimeSnapshot ts{};
    if (!hooks_.inspect_time(ts, hooks_.context)) {
        emit_line("ERR time query failed");
        return;
    }
    emit_str("TIME ticks=");
    emit_u64(ts.ticks);
    emit_str(" hz=");
    emit_u64(ts.clock_hz);
    emit_str(" epoch=");
    emit_u64(ts.epoch_seconds);
    emit_str(".");
    emit_u64(ts.epoch_nanoseconds);
    emit_str(ts.epoch_synchronized ? " sync=yes\n" : " sync=no\n");
}

void Session::cmd_io(int argc, const char* const* argv) {
    if (!hooks_.inspect_io) {
        emit_line("ERR io unavailable");
        return;
    }
    const char* arg = argc > 1 ? argv[1] : "";
    if (!hooks_.inspect_io(arg, writer_, writer_ctx_, hooks_.context)) {
        emit_line("ERR io query failed");
    }
}

void Session::cmd_vault(int argc, const char* const* argv) {
    if (!hooks_.inspect_vault) {
        emit_line("ERR vault unavailable");
        return;
    }
    if (!hooks_.inspect_vault(argc, argv, writer_, writer_ctx_, hooks_.context)) {
        emit_line("ERR vault command failed");
    }
}

void Session::cmd_guard() {
    if (!hooks_.inspect_guard) {
        emit_line("ERR guard unavailable");
        return;
    }
    if (!hooks_.inspect_guard(writer_, writer_ctx_, hooks_.context)) {
        emit_line("ERR guard query failed");
    }
}

void Session::cmd_flightlog(int argc, const char* const* argv) {
    if (!hooks_.inspect_flightlog) {
        emit_line("ERR flightlog unavailable");
        return;
    }
    const char* arg = argc > 1 ? argv[1] : "";
    if (!hooks_.inspect_flightlog(arg, writer_, writer_ctx_, hooks_.context)) {
        emit_line("ERR flightlog query failed");
    }
}

void Session::cmd_runs() {
    emit_line("RUNS");
    for (size_t i = 0; i < max_jobs; ++i) {
        const auto& job = jobs_[i];
        if (job.state == JobState::free_slot) continue;
        const uint32_t id = (static_cast<uint32_t>(job.generation) << 8) | static_cast<uint32_t>(i);
        emit_str("  id=");
        emit_hex32(id);
        emit_str(" name=");
        emit_str(job.name[0] ? job.name : "job");
        emit_str(" state=");
        emit_str(job_state_name(job.state));
        emit_str(" steps=");
        emit_u64(job.steps_run);
        emit_raw("\n", 1);
    }
}

void Session::cmd_abort(int argc, const char* const* argv) {
    if (argc < 2) {
        emit_line("ERR usage: abort <job_id>");
        return;
    }
    uint32_t id = 0;
    if (!parse_u32(argv[1], id)) {
        emit_line("ERR invalid job id");
        return;
    }
    if (cancel_job(id)) {
        emit_str("OK abort requested ");
        emit_hex32(id);
        emit_raw("\n", 1);
    } else {
        emit_line("ERR job not active or stale generation");
    }
}

void Session::cmd_script(int argc, const char* const* argv) {
    if (!hooks_.run_script) {
        emit_line("ERR script unavailable");
        return;
    }
    if (!hooks_.run_script(argc, argv, writer_, writer_ctx_, hooks_.context)) {
        emit_line("ERR script execution failed");
    }
}

} // namespace rtos::monitor
