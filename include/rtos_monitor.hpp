#pragma once
/**
 * @file rtos_monitor.hpp
 * @brief Modular NexRT Diagnostic Monitor (RTOS_MONITOR; NXR-038, NXR-039, NXR-040).
 *
 * Defaults OFF in minimal and flight presets; developer may opt in on any build.
 * Uses original NexRT command names:
 *   help, sched, cycles, time, io, vault, guard, flightlog, runs, abort, script
 */

#include "rtos.hpp"
#include "rtos_config.hpp"

#include <stddef.h>
#include <stdint.h>

namespace rtos::monitor {

/// Static sizing limits for bounded monitor sessions (NXR-038, NXR-039).
constexpr size_t max_line_bytes = 128;
constexpr size_t max_tokens = 8;
constexpr size_t max_jobs = 4;
constexpr size_t max_snapshot_threads = 16;

/// Output transport callback. Returns number of bytes accepted (may be < len under backpressure).
using WriteFn = size_t (*)(const char* data, size_t len, void* context);

/// Cooperative background job step callback. Return true while still running, false when done.
using JobStepFn = bool (*)(bool cancel_requested, void* job_ctx, WriteFn write_fn, void* write_ctx);

/// Job lifecycle states (NXR-039).
enum class JobState : uint8_t {
    free_slot = 0,
    running = 1,
    cancel_requested = 2,
    completed = 3,
    canceled = 4,
};

/// Generation-tagged static background job slot.
struct JobSlot {
    uint16_t generation{};
    JobState state{JobState::free_slot};
    char name[16]{};
    JobStepFn step{};
    void* context{};
    uint32_t steps_run{};
};

/// Time inspection snapshot returned by board/kernel hook for `time`.
struct TimeSnapshot {
    uint32_t ticks{};
    uint32_t clock_hz{};
    uint64_t epoch_seconds{};
    uint32_t epoch_nanoseconds{};
    bool epoch_synchronized{};
};

/// Subsystem inspection callbacks supplied by the bench board/application (NXR-040).
struct Hooks {
    /// Snapshot scheduler/thread table (`sched`, `cycles`).
    SystemInfo (*snapshot_threads)(ThreadInfo* out, size_t cap, void* ctx){};
    /// Inspect monotonic + epoch time (`time`).
    bool (*inspect_time)(TimeSnapshot& out, void* ctx){};
    /// Inspect peripheral/IO status (`io`). Writes bounded text via emit().
    bool (*inspect_io)(const char* arg, WriteFn emit, void* emit_ctx, void* ctx){};
    /// Inspect/manipulate flash storage (`vault`).
    bool (*inspect_vault)(int argc, const char* const* argv, WriteFn emit, void* emit_ctx, void* ctx){};
    /// Inspect boot handoff / watchdog / health guard (`guard`).
    bool (*inspect_guard)(WriteFn emit, void* emit_ctx, void* ctx){};
    /// Inspect binary flight recorder (`flightlog`).
    bool (*inspect_flightlog)(const char* arg, WriteFn emit, void* emit_ctx, void* ctx){};
    /// Execute a sandboxed script (`script`).
    bool (*run_script)(int argc, const char* const* argv, WriteFn emit, void* emit_ctx, void* ctx){};
    void* context{};
};

/// Static-storage NexRT diagnostic monitor session (NXR-038, NXR-039, NXR-040).
class Session {
public:
    Session() = default;

    /// Bind output writer and optional subsystem inspection hooks.
    void initialize(WriteFn writer, void* writer_ctx, const Hooks& hooks);

    /// Feed raw input bytes (e.g., from UART RX). Handles line buffering, backspace,
    /// CR/LF termination, and NXR-038 overflow discard-until-newline with a single error.
    void feed_bytes(const char* data, size_t len);

    /// Execute one complete command line directly (useful for automated tests and batch scripts).
    bool execute_line(const char* line);

    /// Spawn a cooperative background job in the fixed static job table (NXR-039).
    /// Returns generation-tagged job ID `(generation << 8) | slot`, or 0 on failure.
    uint32_t spawn_job(const char* name, JobStepFn step_fn, void* job_ctx);

    /// Request cooperative cancellation of a generation-tagged job ID (NXR-039).
    bool cancel_job(uint32_t job_id);

    /// Advance all active background jobs by one cooperative quantum.
    /// Returns the number of jobs that executed a step.
    size_t poll_jobs();

    /// Total output bytes dropped due to writer backpressure (NXR-038).
    uint32_t dropped_output_bytes() const { return dropped_output_; }

    /// Total line-overflow events detected and cleanly recovered (NXR-038).
    uint32_t line_overflow_count() const { return overflow_count_; }

    /// Total commands dispatched since initialize().
    uint32_t command_count() const { return command_count_; }

private:
    void emit_raw(const char* data, size_t len);
    void emit_str(const char* s);
    void emit_u64(uint64_t v);
    void emit_hex32(uint32_t v);
    void emit_line(const char* s);

    void cmd_help();
    void cmd_sched();
    void cmd_cycles();
    void cmd_time();
    void cmd_io(int argc, const char* const* argv);
    void cmd_vault(int argc, const char* const* argv);
    void cmd_guard();
    void cmd_flightlog(int argc, const char* const* argv);
    void cmd_runs();
    void cmd_abort(int argc, const char* const* argv);
    void cmd_script(int argc, const char* const* argv);

    WriteFn writer_{};
    void* writer_ctx_{};
    Hooks hooks_{};

    char line_buf_[max_line_bytes + 1]{};
    size_t line_len_{};
    bool line_overflowed_{};

    JobSlot jobs_[max_jobs]{};
    uint16_t next_generation_{1};

    uint32_t dropped_output_{};
    uint32_t overflow_count_{};
    uint32_t command_count_{};
};

} // namespace rtos::monitor
