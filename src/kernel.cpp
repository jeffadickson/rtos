#include "scheduler.hpp"
#include "rtos_port.hpp"
#if RTOS_SYNC
#include "rtos_sync.hpp"
#include "rtos_mutex.hpp"
#endif
namespace {
rtos::detail::Scheduler scheduler;
rtos::Thread idle_thread;
rtos::Stack<RTOS_IDLE_STACK_BYTES> idle_stack;
rtos::Thread* current;
bool running;
#if RTOS_SYNC
bool handling_interrupt;
#endif
uint32_t period, clock_hz;
uint64_t deadline;
#if RTOS_METRICS
uint64_t boot_time, last_account;
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
void idle(void*) { for(;;) rtos::port::idle(); }
extern "C" [[noreturn]] void rtos_entry() { current->entry(current->argument); rtos::exit(); }
bool guard_ok(const rtos::Thread& t) {
#if RTOS_STACK_GUARD
    return *reinterpret_cast<const volatile uint32_t*>(t.stack_begin) == guard_value;
#else
    (void)t; return true;
#endif
}
void initialize(rtos::Thread& t, rtos::Entry e, void* arg, uint8_t priority,
                void* stack, size_t bytes, const char* name) {
    auto* frame=rtos::port::initialize_context(stack,bytes,&rtos_entry);
    t.context=frame; t.entry=e; t.argument=arg; t.priority=priority; t.state=rtos::State::ready;
#if RTOS_SYNC
    t.effective_priority=priority;
#endif
    t.name=name; t.stack_begin=reinterpret_cast<uintptr_t>(stack); t.stack_bytes=bytes;
#if RTOS_METRICS
    t.runtime_cycles=0; t.dispatches=0;
#endif
#if RTOS_STACK_GUARD
    *static_cast<uint32_t*>(stack)=guard_value;
#endif
}
#if RTOS_METRICS
void account(uint64_t now) {
    if (current) current->runtime_cycles += now-last_account;
    last_account=now;
}
#endif
}
namespace rtos {
bool create(Thread& t, Entry e, void* arg, uint8_t priority, void* stack, size_t bytes, const char* name) {
    const auto begin = reinterpret_cast<uintptr_t>(stack);
    const auto control = reinterpret_cast<uintptr_t>(&t);
    if (running || t.state != State::unused || !e || !stack || bytes < 512 ||
        (begin & 15) || (bytes & 15) || bytes > UINTPTR_MAX-begin ||
        detail::ranges_overlap(begin,bytes,control,sizeof(Thread)) ||
        scheduler.conflicts(begin,bytes) || scheduler.conflicts(control,sizeof(Thread)) ||
        !scheduler.add(t)) return false;
    initialize(t,e,arg,priority,stack,bytes,name);
    return true;
}
[[noreturn]] void start(uint32_t hz, uint32_t tick_hz) {
    (void)port::mask_interrupts();
    if (running || !tick_hz || hz < tick_hz ||
        scheduler.conflicts(reinterpret_cast<uintptr_t>(idle_stack.words),sizeof(idle_stack)) ||
        scheduler.conflicts(reinterpret_cast<uintptr_t>(&idle_thread),sizeof(idle_thread))) stop();
    initialize(idle_thread,idle,nullptr,0,idle_stack.words,sizeof(idle_stack),"idle");
    period=hz/tick_hz; clock_hz=hz;
    running=true;
    port::install_vector();
    const auto now=time_now();
#if RTOS_METRICS
    boot_time=last_account=now;
#endif
    deadline=now+period; arm();
    port::enable_timer();
    port::reschedule();
    stop();
}
void yield() { if (running) port::reschedule(); }
bool sleep_for(Tick delay) {
    if (!running || delay > 0x7fffffffu) return false;
    Lock lock;
    if (delay) { current->wake=scheduler.now+delay; current->state=State::sleeping; }
    yield();
    return true;
}
Tick ticks() { Lock lock; return scheduler.now; }
[[noreturn]] void exit() {
    Lock lock;
    if (!current || current == &idle_thread) stop();
#if RTOS_SYNC
    Mutex::owner_exit(*current);
#endif
    current->state=State::finished;
    yield(); stop();
}
bool set_priority(Thread& t, uint8_t priority) {
    Lock lock;
    if (!scheduler.contains(t) || t.state == State::finished) return false;
    t.priority=priority;
#if RTOS_SYNC
    Mutex::priority_changed(t);
#endif
    yield();
    return true;
}
SystemInfo snapshot(ThreadInfo* output, size_t capacity) {
    Lock lock;
    SystemInfo result{};
    result.ticks=scheduler.now; result.clock_hz=clock_hz; result.timer_period_cycles=period;
    result.required_threads=scheduler.count+(running ? 1u : 0u);
#if RTOS_METRICS
    result.metrics_enabled=true;
    if (running) { const auto now=time_now(); account(now); result.elapsed_cycles=now-boot_time; }
#endif
    if (!output) return result;
    for (size_t i=0; i<result.required_threads && i<capacity; ++i) {
        auto& t = i<scheduler.count ? *scheduler.threads[i] : idle_thread;
        auto& row=output[i];
        row.id=&t; row.name=t.name; row.state=t.state; row.priority=t.priority;
        row.effective_priority=detail::Scheduler::priority(t);
        row.current=(&t==current); row.idle=(&t==&idle_thread); row.guard_ok=guard_ok(t);
        row.stack_bytes=t.stack_bytes;
#if RTOS_METRICS
        row.runtime_cycles=t.runtime_cycles; row.dispatches=t.dispatches;
#else
        row.runtime_cycles=0; row.dispatches=0;
#endif
        ++result.written_threads;
    }
    return result;
}
}
extern "C" uint32_t* rtos_trap(uint32_t* frame, uint32_t cause) {
#if RTOS_SYNC
    handling_interrupt=true;
#endif
    if (current) {
        current->context=frame;
        if (!guard_ok(*current)) stop();
    }
    if (rtos::port::is_yield(cause)) rtos::port::resume_after_yield(frame);
    else if (rtos::port::is_tick(cause)) {
        const auto now=time_now();
        const auto elapsed=rtos::detail::elapsed_ticks(now,deadline,period);
        // Fail closed if masking exceeded the wrap-safe sleep comparison horizon.
        if (elapsed > 0x7fffffffu) stop();
        deadline+=elapsed*period; scheduler.advance(static_cast<rtos::Tick>(elapsed));
        arm();
    } else stop();
#if RTOS_METRICS
    // Account trap work to the interrupted thread; restore/entry overhead belongs to the next.
    account(time_now());
#endif
    auto* next=scheduler.pick(current, rtos::port::is_yield(cause) || RTOS_ROUND_ROBIN);
    if (!next) next=&idle_thread;
    if (!guard_ok(*next)) stop();
#if RTOS_METRICS
    ++next->dispatches;
#endif
    current=next;
#if RTOS_SYNC
    handling_interrupt=false;
#endif
    return current->context;
}

#if RTOS_SYNC
namespace rtos::detail {
CriticalState critical_enter(){return port::mask_interrupts();}
void critical_leave(CriticalState state){port::restore_interrupts(static_cast<port::InterruptState>(state));}
Thread* calling_thread(){return running&&!handling_interrupt&&current!=&idle_thread?current:nullptr;}
bool interrupt_context(){return handling_interrupt;}
bool synchronization_running(){return running;}
Tick synchronization_now(){return scheduler.now;}
void park_thread(){port::reschedule();}
void schedule_ready(){if(running&&!handling_interrupt)port::reschedule();}
}
#endif
