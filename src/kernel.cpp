#include "scheduler.hpp"
#include "rtos_port.hpp"
#include "rtos/detail/stack_watermark.hpp"
#if RTOS_SYNC
#include "rtos_sync.hpp"
#include "rtos_mutex.hpp"
#endif
#if RTOS_HW_SCHED
#include "rtos_hw_sched.hpp"
#endif

namespace {
rtos::detail::Scheduler scheduler;
// Idle owns private boot-static storage and consumes no application registry slot.
rtos::Thread idle_thread;
rtos::Stack<RTOS_IDLE_STACK_BYTES> idle_stack;
rtos::Thread* current;
bool running;
bool yield_requested;
rtos::IrqHandler external_irq_handler;
void* external_irq_context;
#if RTOS_SYNC
bool handling_interrupt;
bool reschedule_pending;
#endif
uint32_t period, clock_hz;
uint64_t deadline;
#if RTOS_METRICS || RTOS_BUDGET_WATCHDOG
uint64_t boot_time, last_account;
#endif
#if RTOS_BUDGET_WATCHDOG
rtos::BudgetFaultHook budget_fault_hook;
#endif
#if RTOS_HW_SCHED
constexpr rtos::hw_sched::MmioDriver hw_driver(RTOS_HWSCHED_BASE);
#endif
constexpr uint32_t guard_value = 0x51accede;
using rtos::port::time_now;
void arm() { rtos::port::arm(deadline); }

struct Lock {
    rtos::port::InterruptState previous;
    Lock() : previous(rtos::port::mask_interrupts()) {}
    ~Lock() { rtos::port::restore_interrupts(previous); }
};

[[noreturn]] void stop() { rtos::port::halt(); }
void idle(void*) {
    for (;;) rtos::port::idle();
}

// Every synthesized frame enters here; returning application entries finish via exit().
extern "C" [[noreturn]] void rtos_entry() {
    current->entry(current->argument);
    rtos::exit();
}

bool guard_ok(const rtos::Thread& t) {
#if RTOS_STACK_GUARD
    return *reinterpret_cast<const volatile uint32_t*>(t.stack_begin) == guard_value;
#else
    (void)t;
    return true;
#endif
}

#if RTOS_HW_SCHED
void sync_hw_slot(const rtos::Thread& t) {
    if (&t == &idle_thread) return;
    hw_driver.configure_thread(t.hw_slot, t.state, t.priority, rtos::detail::Scheduler::priority(t));
}
#endif

void initialize(rtos::Thread& t, rtos::Entry e, void* arg, uint8_t priority,
                void* stack, size_t bytes, const char* name) {
    // No allocation or runtime constructors. context.S owns the exact frame layout.
#if RTOS_STACK_WATERMARK
    rtos::detail::paint_stack(static_cast<uint32_t*>(stack), bytes / sizeof(uint32_t));
#endif
    auto* frame = rtos::port::initialize_context(stack, bytes, &rtos_entry);
    t.context = frame;
    t.entry = e;
    t.argument = arg;
    t.priority = priority;
    t.state = rtos::State::ready;
#if RTOS_SYNC
    t.effective_priority = priority;
#endif
    t.name = name;
    t.stack_begin = reinterpret_cast<uintptr_t>(stack);
    t.stack_bytes = bytes;
#if RTOS_METRICS
    t.runtime_cycles = 0;
    t.dispatches = 0;
#endif
#if RTOS_BUDGET_WATCHDOG
    t.budget_cycles = 0;
    t.slice_cycles = 0;
#endif
#if RTOS_STACK_GUARD
    *static_cast<uint32_t*>(stack) = guard_value;
#endif
}

#if RTOS_METRICS || RTOS_BUDGET_WATCHDOG
void account(uint64_t now) {
    // Snapshot and trap callers both mask IRQs. Account once to avoid double counting.
    const uint64_t delta = now - last_account;
    if (current) {
#if RTOS_METRICS
        current->runtime_cycles += delta;
#endif
#if RTOS_BUDGET_WATCHDOG
        if (current != &idle_thread) {
            current->slice_cycles += delta;
        }
#endif
    }
    last_account = now;
}
#endif

#if RTOS_BUDGET_WATCHDOG
void check_budget_watchdog(rtos::Thread* thread) {
    if (!thread || thread == &idle_thread || thread->state != rtos::State::ready) return;
    if (thread->budget_cycles != 0 && thread->slice_cycles > thread->budget_cycles) {
        const uint64_t used = thread->slice_cycles;
#if RTOS_SYNC
        rtos::Mutex::owner_exit(*thread);
#endif
        thread->state = rtos::State::quarantined;
        thread->slice_cycles = 0;
#if RTOS_HW_SCHED
        sync_hw_slot(*thread);
#endif
        if (budget_fault_hook) budget_fault_hook(*thread, used);
    }
}
#endif
} // namespace

namespace rtos {
bool create(Thread& t, Entry e, void* arg, uint8_t priority, void* stack, size_t bytes, const char* name) {
    // Reject all extents before publication/frame writes. Actual pointer validity
    // cannot be established by an unprotected machine-mode kernel.
    const auto begin = reinterpret_cast<uintptr_t>(stack);
    const auto control = reinterpret_cast<uintptr_t>(&t);
    if (running || t.state != State::unused || !e || !stack || bytes < 512 ||
        (begin & 15) || (bytes & 15) || bytes > UINTPTR_MAX - begin ||
        detail::ranges_overlap(begin, bytes, control, sizeof(Thread)) ||
        scheduler.conflicts(begin, bytes) || scheduler.conflicts(control, sizeof(Thread)) ||
        !scheduler.add(t)) {
        return false;
    }
    initialize(t, e, arg, priority, stack, bytes, name);
#if RTOS_HW_SCHED
    t.hw_slot = static_cast<uint8_t>(scheduler.count - 1);
    sync_hw_slot(t);
#endif
    return true;
}

bool register_external_irq(IrqHandler handler, void* context) {
    Lock lock;
    if (running || !handler || external_irq_handler) return false;
    external_irq_handler = handler;
    external_irq_context = context;
    return true;
}

bool set_execution_budget(Thread& t, uint64_t max_slice_cycles) {
#if RTOS_BUDGET_WATCHDOG
    Lock lock;
    if (!scheduler.contains(t) || t.state == State::finished || t.state == State::quarantined) {
        return false;
    }
    t.budget_cycles = max_slice_cycles;
    t.slice_cycles = 0;
#if RTOS_HW_SCHED
    const uint32_t hw_budget = max_slice_cycles > UINT32_MAX
                                   ? UINT32_MAX
                                   : static_cast<uint32_t>(max_slice_cycles);
    hw_driver.set_budget(t.hw_slot, hw_budget);
#endif
    return true;
#else
    (void)t;
    (void)max_slice_cycles;
    return false;
#endif
}

bool register_budget_fault_hook(BudgetFaultHook hook) {
#if RTOS_BUDGET_WATCHDOG
    Lock lock;
    if (running) return false;
    budget_fault_hook = hook;
    return true;
#else
    (void)hook;
    return false;
#endif
}

[[noreturn]] void start(uint32_t hz, uint32_t tick_hz) {
    // First dispatch runs from the boot stack with current==nullptr. Its bootstrap
    // frame is discarded; application stacks already hold synthesized entry frames.
    (void)port::mask_interrupts();
    if (running || !tick_hz || hz < tick_hz ||
        scheduler.conflicts(reinterpret_cast<uintptr_t>(idle_stack.words), sizeof(idle_stack)) ||
        scheduler.conflicts(reinterpret_cast<uintptr_t>(&idle_thread), sizeof(idle_thread))) {
        stop();
    }
    initialize(idle_thread, idle, nullptr, 0, idle_stack.words, sizeof(idle_stack), "idle");
    period = hz / tick_hz;
    clock_hz = hz;
    running = true;
    port::install_vector();
    const auto now = time_now();
#if RTOS_METRICS || RTOS_BUDGET_WATCHDOG
    boot_time = last_account = now;
#endif
    deadline = now + period;
    arm();
    port::enable_timer();
    if (external_irq_handler) port::enable_external_irq();
#if RTOS_HW_SCHED
    hw_driver.start(period, true, RTOS_ROUND_ROBIN != 0);
#endif
    port::reschedule();
    stop();
}

void yield() {
    if (!running) return;
    Lock lock;
    yield_requested = true;
    port::reschedule();
}

bool sleep_for(Tick delay) {
    if (!running || delay > 0x7fffffffu) return false;
    if (!delay) {
        yield();
        return true;
    }
    // A suspended context retains its port mask; restore the caller's prior mask
    // after resume (RISC-V MIE or Cortex-M BASEPRI).
    Lock lock;
    current->wake = scheduler.now + delay;
    current->state = State::sleeping;
#if RTOS_BUDGET_WATCHDOG
    current->slice_cycles = 0;
#endif
#if RTOS_HW_SCHED
    hw_driver.set_wake(current->hw_slot, hw_driver.mtime() + static_cast<uint64_t>(delay) * period);
    sync_hw_slot(*current);
#endif
    port::reschedule();
    return true;
}

Tick ticks() {
    Lock lock;
    return scheduler.now;
}

[[noreturn]] void exit() {
    Lock lock;
    if (!current || current == &idle_thread) stop();
#if RTOS_SYNC
    Mutex::owner_exit(*current);
#endif
    current->state = State::finished;
#if RTOS_HW_SCHED
    sync_hw_slot(*current);
#endif
    port::reschedule();
    stop();
}

bool set_priority(Thread& t, uint8_t priority) {
    Lock lock;
    if (!scheduler.contains(t) || t.state == State::finished || t.state == State::quarantined) {
        return false;
    }
    t.priority = priority;
#if RTOS_SYNC
    Mutex::priority_changed(t);
#endif
#if RTOS_HW_SCHED
    sync_hw_slot(t);
#endif
    if (running) port::reschedule();
    return true;
}

SystemInfo snapshot(ThreadInfo* output, size_t capacity) {
    Lock lock;
    SystemInfo result{};
    result.ticks = scheduler.now;
    result.clock_hz = clock_hz;
    result.timer_period_cycles = period;
    result.required_threads = scheduler.count + (running ? 1u : 0u);
#if RTOS_METRICS
    result.metrics_enabled = true;
    if (running) {
        const auto now = time_now();
        account(now);
        result.elapsed_cycles = now - boot_time;
    }
#endif
    if (!output) return result;
    for (size_t i = 0; i < result.required_threads && i < capacity; ++i) {
        auto& t = i < scheduler.count ? *scheduler.threads[i] : idle_thread;
        auto& row = output[i];
        row.id = &t;
        row.name = t.name;
        row.state = t.state;
        row.priority = t.priority;
        row.effective_priority = detail::Scheduler::priority(t);
        row.current = (&t == current);
        row.idle = (&t == &idle_thread);
        row.guard_ok = guard_ok(t);
        row.stack_bytes = t.stack_bytes;
#if RTOS_METRICS
#if RTOS_HW_SCHED
        if (&t != &idle_thread) {
            row.runtime_cycles = hw_driver.runtime(t.hw_slot);
            row.dispatches = hw_driver.dispatches(t.hw_slot);
        } else {
            row.runtime_cycles = t.runtime_cycles;
            row.dispatches = t.dispatches;
        }
#else
        row.runtime_cycles = t.runtime_cycles;
        row.dispatches = t.dispatches;
#endif
#else
        row.runtime_cycles = 0;
        row.dispatches = 0;
#endif
        ++result.written_threads;
    }
    return result;
}

bool stack_usage(const Thread& thread, StackUsage& output, size_t budget_bytes) {
    Lock lock;
    if (!scheduler.contains(thread) && !(running && &thread == &idle_thread)) return false;
    StackUsage result{};
    result.capacity_bytes = thread.stack_bytes;
    result.guard_ok = guard_ok(thread);
#if RTOS_STACK_WATERMARK
    result.watermark_enabled = true;
    constexpr size_t guard_words = RTOS_STACK_GUARD ? 1 : 0;
    const auto* words = reinterpret_cast<const volatile uint32_t*>(thread.stack_begin);
    const auto scan = detail::scan_stack(words + guard_words, thread.stack_bytes / 4 - guard_words, budget_bytes / 4);
    result.inspected_bytes = scan.read_words * 4;
    result.untouched_bytes = scan.untouched_words * 4;
    result.used_bytes = thread.stack_bytes - result.untouched_bytes;
    result.complete = scan.complete;
#else
    (void)budget_bytes;
#endif
    output = result;
    return true;
}
} // namespace rtos

#if defined(RTOS_ARM_STRESS_WITNESS) && RTOS_ARM_STRESS_WITNESS
/// Validation hook runs only after assembly has saved the full integer context.
extern "C" void rtos_arm_stress_tick(const uint32_t* frame, uint64_t elapsed);
#endif

extern "C" uint32_t* rtos_trap(uint32_t* frame, uint32_t cause) {
    // Called only by context.S after integer state is saved and the port has
    // serialized scheduling interrupts (RISC-V MIE or Cortex-M BASEPRI).
    // Return a selected frame pointer; assembly changes sp after this call unwinds.
#if RTOS_SYNC
    handling_interrupt = true;
    reschedule_pending = false;
#endif
    if (current) {
        if (frame) current->context = frame;
        if (!guard_ok(*current)) stop();
    }
    const bool explicit_yield = yield_requested;
    yield_requested = false;
    bool rotate = explicit_yield;

    if (rtos::port::is_yield(cause)) {
        if (current && frame) rtos::port::resume_after_yield(frame);
    } else if (rtos::port::is_tick(cause)) {
        const auto now = time_now();
        const auto elapsed = rtos::detail::elapsed_ticks(now, deadline, period);
        // Fail closed if masking exceeded the wrap-safe sleep comparison horizon.
        if (elapsed > 0x7fffffffu) stop();
#if defined(RTOS_ARM_STRESS_WITNESS) && RTOS_ARM_STRESS_WITNESS
        rtos_arm_stress_tick(frame, elapsed);
#endif
        deadline += elapsed * period;
        scheduler.advance(static_cast<rtos::Tick>(elapsed));
        arm();
        rotate = (RTOS_ROUND_ROBIN != 0);
    } else if (rtos::port::is_external_irq(cause)) {
        if (!external_irq_handler) stop();
        external_irq_handler(external_irq_context);
        rotate = false;
    } else {
        stop();
    }

#if RTOS_METRICS || RTOS_BUDGET_WATCHDOG
    // Account trap work to the interrupted thread; restore/entry overhead belongs to the next.
    account(time_now());
#endif
#if RTOS_BUDGET_WATCHDOG
    check_budget_watchdog(current);
#endif

    rtos::Thread* next = nullptr;
#if RTOS_HW_SCHED
    // Reconcile any hardware-expired deadlines or hardware WCET faults first.
    const uint32_t expired = hw_driver.expired_mask();
    if (expired) {
        hw_driver.clear_expired(expired);
        for (unsigned i = 0; i < scheduler.count; ++i) {
            if ((expired & (1u << i)) != 0) {
                auto& t = *scheduler.threads[i];
#if RTOS_SYNC
                if (t.state == rtos::State::blocked) {
                    rtos::detail::expire_wait(t, t.wake);
                }
#endif
                if (t.state == rtos::State::sleeping) {
                    t.state = rtos::State::ready;
                }
                sync_hw_slot(t);
            }
        }
    }
    const uint32_t faults = hw_driver.fault_mask();
    if (faults) {
        hw_driver.clear_faults(faults);
        for (unsigned i = 0; i < scheduler.count; ++i) {
            if ((faults & (1u << i)) != 0) {
                auto& t = *scheduler.threads[i];
#if RTOS_SYNC
                rtos::Mutex::owner_exit(t);
#endif
                t.state = rtos::State::quarantined;
                sync_hw_slot(t);
            }
        }
    }
    hw_driver.commit(rotate);
    const auto hw_status = hw_driver.status();
    if (hw_status.current_valid && hw_status.current_tid < scheduler.count) {
        next = scheduler.threads[hw_status.current_tid];
    }
#else
    next = scheduler.pick(current, rotate);
#endif
    if (!next) next = &idle_thread;
    if (!guard_ok(*next)) stop();
#if RTOS_BUDGET_WATCHDOG
    if (next != current && current) current->slice_cycles = 0;
#endif
#if RTOS_METRICS
    ++next->dispatches;
#endif
    current = next;
    rtos::port::configure_protection(current->stack_begin, current->stack_bytes);
#if RTOS_SYNC
    reschedule_pending = false;
    handling_interrupt = false;
#endif
    return current->context;
}

#if RTOS_SYNC
namespace rtos::detail {
CriticalState critical_enter() { return port::mask_interrupts(); }
void critical_leave(CriticalState state) { port::restore_interrupts(static_cast<port::InterruptState>(state)); }
Thread* calling_thread() { return running && !handling_interrupt && current != &idle_thread ? current : nullptr; }
bool interrupt_context() { return handling_interrupt; }
bool synchronization_running() { return running; }
Tick synchronization_now() { return scheduler.now; }
void park_thread() { port::reschedule(); }
void schedule_ready() {
    if (!running) return;
    if (handling_interrupt) {
        reschedule_pending = true;
        return;
    }
    port::reschedule();
}
#if RTOS_HW_SCHED
void hw_sync_thread(const Thread& t) {
    if (t.state == State::blocked) {
        const Tick remaining = static_cast<Tick>(t.wake - scheduler.now);
        hw_driver.set_wake(t.hw_slot, hw_driver.mtime() + static_cast<uint64_t>(remaining) * period);
    }
    sync_hw_slot(t);
}
#endif
} // namespace rtos::detail
#endif
