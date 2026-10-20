/** @file
 * @brief Bounded real-kernel Cortex-M3/QEMU stress, with boot-static storage.
 * Semihosting is a simulation report/exit channel, not a production driver.
 */
#include "rtos_ipc.hpp"
extern "C" uint32_t rtos_arm_callee_canary(uint32_t inject);
extern "C" uint32_t rtos_arm_irq_canary(uint32_t inject);
extern "C" uint32_t rtos_arm_padding_canary();
extern "C" char rtos_arm_irq_live_begin[],rtos_arm_irq_live_end[];
#if !RTOS_ARM_STRESS_WITNESS
#error "ARM stress requires validation witnesses; ordinary applications leave them disabled"
#endif
/// Called only with an aligned stack by the assembly callee-saved harness.
extern "C" void rtos_arm_canary_park(){(void)rtos::sleep_for(2);}
#ifndef RTOS_STRESS_ROUNDS
#define RTOS_STRESS_ROUNDS 10000
#endif
#ifndef RTOS_STRESS_QUEUE_CAPACITY
#define RTOS_STRESS_QUEUE_CAPACITY 8
#endif
namespace {
constexpr uint32_t rounds=RTOS_STRESS_ROUNDS;
static_assert(rounds>=100 && rounds<=1000000);
/// ARM semihosting ABI: r0 operation, r1 argument, BKPT 0xAB, r0 return value.
uintptr_t host_call(uintptr_t operation,const void* pointer){
    register uintptr_t code asm("r0")=operation;
    register const void* argument asm("r1")=pointer;
    asm volatile("bkpt 0xab" : "+r"(code) : "r"(argument) : "memory","cc");
    return code;
}
void report(const char* text){(void)host_call(4,text);}
[[noreturn]] void finish(uint32_t status){
    const uint32_t block[]{0x20026,status};
    (void)host_call(0x20,block);
    for(;;)asm volatile("wfi");
}
[[noreturn]] void fail(const char* message){report("FAIL: ");report(message);report("\n");finish(1);}
void check(bool condition,const char* message){if(!condition)fail(message);}
/// Read the actual Cortex-M scheduling mask, not a host critical-depth model.
uint32_t read_mask(){uint32_t value;asm volatile("mrs %0, basepri":"=r"(value));return value;}
/// Test-only privileged mask changes; SVC priority zero remains callable.
void write_mask(uint32_t value){asm volatile("msr basepri, %0\ndsb\nisb"::"r"(value):"memory");}
void number(uint32_t value){
    char text[12]{};unsigned at=10;
    do{text[--at]=char('0'+value%10);value/=10;}while(value);
    report(text+at);
}
struct Message {uint32_t producer,sequence,value;};
static rtos::Thread threads[9];
static rtos::Thread unregistered;
static rtos::Stack<2048> stacks[9];
static rtos::Queue<Message,RTOS_STRESS_QUEUE_CAPACITY> fifo;
static rtos::Queue<Message,2> timeout_fifo;
static rtos::Semaphore owner_go(0,1),high_go(0,1),medium_go(0,1);
static rtos::Semaphore masked_waiter_go(0,1);
static rtos::Semaphore ping(1,1),pong(0,1),empty(0,1);
static rtos::EventFlags phase,completed;
static rtos::Mutex inheritance,counts;
static rtos::AtomicU32 inherited,inheritance_done,stop_cpu;
static uint32_t shared{},produced{},consumed{},cpu_rounds{};
constexpr uint32_t begin_stress=1,owner_locked=2,high_done=4;
constexpr uint32_t masked_waiter_ready=128,masked_waiter_done=256;
// ISR-only writes, thread observations; no thread waits or kernel calls in hooks.
static volatile uint32_t live_window_hits{},first_elapsed{};
static volatile bool catchup_pending{},counter_seen{},counter_bad{},counter_wrapped{};
static volatile uint32_t initial_low{};
static uint64_t previous_counter{}; // Accessed only by serialized time_now callbacks.

/// Lowest-priority owner must inherit the blocked high-priority waiter's priority.
void low_owner(void*){
    check(owner_go.acquire(1000)==rtos::Result::ok,"owner start");
    check(inheritance.lock()==rtos::Result::ok,"owner lock");
    ++shared;
    check(phase.set(owner_locked)==rtos::Result::ok,"owner notification");
    rtos::ThreadInfo rows[10];const auto info=rtos::snapshot(rows,10);
    for(size_t i=0;i<info.written_threads;++i)
        if(rows[i].current)inherited.store(rows[i].priority==1&&rows[i].effective_priority==4);
    check(inherited.load()==1,"missing priority donation");
    check(inheritance.unlock()==rtos::Result::ok,"owner unlock");
}
void high_waiter(void*){
    check(masked_waiter_go.acquire(1000)==rtos::Result::ok,"masked waiter start");
    check(phase.set(masked_waiter_ready)==rtos::Result::ok,"masked waiter ready");
    check(empty.acquire(3)==rtos::Result::timeout,"masked waiter expiry");
    check(phase.set(masked_waiter_done)==rtos::Result::ok,"masked waiter completion");
    check(high_go.acquire(1000)==rtos::Result::ok,"high start");
    check(inheritance.lock(100)==rtos::Result::ok,"priority inversion timeout");
    ++shared;
    check(inheritance.unlock()==rtos::Result::ok,"high unlock");
    inheritance_done.store(1);
    check(phase.set(high_done)==rtos::Result::ok,"high completion");
}
/// A ready intermediate-priority CPU hog exposes missing priority inheritance.
/// Later it never yields during load: timer slicing must let the IPC peers run.
void cpu_load(void*){
    check(medium_go.acquire(1000)==rtos::Result::ok,"medium start");
    uint32_t value=1;
    while(!inheritance_done.load()){
        value=value*1664525u+1013904223u;
        asm volatile("" : "+r"(value));
    }
    uint32_t observed=0;
    check(phase.wait_any(begin_stress,observed,1000)==rtos::Result::ok,"CPU start");
    while(!stop_cpu.load()){
        for(unsigned i=0;i<128;++i)value=value*1664525u+1013904223u;
        asm volatile("" : "+r"(value));
        ++cpu_rounds;
    }
    check(completed.set(16)==rtos::Result::ok,"CPU completion");
}
void start_gate(){uint32_t bits=0;check(phase.wait_any(begin_stress,bits,1000)==rtos::Result::ok,"start gate");}
/// Producers retain independent state across queue waits, timer IRQs and yields.
void producer(void* tag){
    start_gate();const auto id=uint32_t(reinterpret_cast<uintptr_t>(tag));
    uint32_t value=0x12340000u+id;
    for(uint32_t sequence=1;sequence<=rounds;++sequence){
        value=value*1664525u+1013904223u+id;
        {
            rtos::LockGuard lock(counts,200);check(lock.owns_lock(),"producer mutex");
            ++produced;
            if((sequence&127)==0)rtos::yield(); // Intentionally contend while owned.
        }
        check(fifo.send({id,sequence,value},200)==rtos::Result::ok,"FIFO send");
        if((sequence&31)==0)rtos::yield();
    }
    check(completed.set(1u<<id)==rtos::Result::ok,"producer completion");
}
/// Per-producer order/value checks detect lost, duplicated or corrupted records.
void consumer(void*){
    start_gate();uint32_t sequence[2]{},values[2]{0x12340000,0x12340001};
    for(uint32_t i=0;i<rounds*2;++i){
        Message message{};check(fifo.receive(message,200)==rtos::Result::ok,"FIFO receive");
        check(message.producer<2,"FIFO producer ID");
        const auto id=message.producer;
        check(message.sequence==++sequence[id],"FIFO sequence");
        values[id]=values[id]*1664525u+1013904223u+id;
        check(message.value==values[id],"FIFO payload/register preservation");
        rtos::LockGuard lock(counts,200);check(lock.owns_lock(),"consumer mutex");++consumed;
    }
    check(sequence[0]==rounds&&sequence[1]==rounds,"FIFO conservation");
    check(completed.set(4)==rtos::Result::ok,"consumer completion");
}
/// Token exchange repeatedly blocks/resumes two real ARM thread stacks.
void token_worker(void* tag){
    start_gate();const bool second=tag!=nullptr;
    auto& take=second?pong:ping;auto& give=second?ping:pong;
    for(uint32_t i=0;i<rounds;++i){
        check(take.acquire(200)==rtos::Result::ok,"semaphore acquire");
        check(give.release()==rtos::Result::ok,"semaphore release");
    }
    check(completed.set(second?8:32)==rtos::Result::ok,"token completion");
}
void coordinator(void*){
    report("ARM stress: real Cortex-M3 kernel, static queues/semaphores/mutexes\n");
    check(rtos_arm_callee_canary(0)==1,"callee-saved register canary");
    check(rtos_arm_callee_canary(1)==0,"callee canary negative probe");
    check(rtos_arm_irq_canary(1)==0,"IRQ canary negative probe");
    check(rtos_arm_irq_canary(2)==0,"NZCV negative probe");
    live_window_hits=0; // Only the positive call may satisfy its live-window witness.
    const auto canary_before=rtos::ticks();
    check(rtos_arm_irq_canary(0)==1,"interrupt register/NZCV canary");
    check(rtos::ticks()!=canary_before,"IRQ canary did not span a tick");
    check(live_window_hits>0,"no IRQ inside live register window");
    check(rtos_arm_padding_canary()==1,"PSP alignment padding");
    report("PASS: register canaries, negative probes, PSP alignment padding\n");
    // CMSDK counter progresses even while BASEPRI masks SysTick. Five complete
    // periods must be accounted on the next tick, rather than one pending bit.
    const uint32_t period=25000000u/RTOS_TICK_HZ;
    uint32_t ready_bits=0;
    check(masked_waiter_go.release()==rtos::Result::ok,"masked waiter release");
    check(phase.wait_any(masked_waiter_ready,ready_bits,100)==rtos::Result::ok,"masked waiter enrollment");
    check(rtos::sleep_for(1)&&empty.snapshot().waiters==1,"masked waiter did not block");
    write_mask(0x80);
    const auto catchup_before=rtos::ticks();
    const auto* timer=reinterpret_cast<volatile uint32_t*>(0x40000004);
    const uint32_t counter_before=*timer;
    while(uint32_t(counter_before-*timer)<period*5u)asm volatile("nop");
    check(rtos::ticks()==catchup_before,"masked timer tick delivered");
    first_elapsed=0;catchup_pending=true;
    write_mask(0);
    while(catchup_pending){
        check(uint32_t(counter_before-*timer)<period*20u,"timer catch-up deadline");
    }
    check(first_elapsed>=5,"first handler did not account missed periods");
    check(empty.snapshot().waiters==0,"catch-up did not expire blocked peer");
    check(phase.wait_any(masked_waiter_done,ready_bits,100)==rtos::Result::ok,"expired peer did not resume");
#if RTOS_ARM_TIMER_INITIAL < 1000000
    check(initial_low>0xff000000u&&counter_wrapped,"initial counter preload/rollover not witnessed");
#endif
    check(counter_seen&&!counter_bad,"extended counter lost monotonicity");
    report("WITNESS: live_hits=");number(live_window_hits);report(" first_elapsed=");number(first_elapsed);
    report(" initial_low=");number(initial_low);report(" epoch_seen=");number(counter_wrapped);report("\n");
    report("PASS: masked timer catch-up and counter continuity\n");
    check(read_mask()==0,"initial thread mask");
    rtos::StackUsage masked{};
    write_mask(0x80);
    check(rtos::stack_usage(threads[0],masked,4)&&read_mask()==0x80,"API restored masked caller");
    write_mask(0x40);
    check(rtos::stack_usage(threads[0],masked,4)&&read_mask()==0x40,"API weakened stronger mask");
    const auto masked_before=rtos::ticks();
    check(empty.acquire(2)==rtos::Result::timeout&&read_mask()==0x40,"mask restoration across suspension");
    check(uint32_t(rtos::ticks()-masked_before)>=2,"timer did not progress on peers");
    write_mask(0);
    check(read_mask()==0,"test mask unwind");
    // Exercise finite-timeout paths before any producer can supply resources.
    Message unchanged{9,8,7};
    check(timeout_fifo.receive(unchanged,2)==rtos::Result::timeout&&unchanged.value==7,"empty FIFO timeout");
    check(timeout_fifo.send({0,1,11})==rtos::Result::ok,"FIFO fill one");
    check(timeout_fifo.send({0,2,22})==rtos::Result::ok,"FIFO fill two");
    check(timeout_fifo.send({0,3,33})==rtos::Result::full,"FIFO full");
    check(timeout_fifo.send({0,3,33},2)==rtos::Result::timeout,"full FIFO timeout");
    check(timeout_fifo.receive(unchanged)==rtos::Result::ok&&unchanged.value==11,"FIFO first");
    check(timeout_fifo.receive(unchanged)==rtos::Result::ok&&unchanged.value==22,"FIFO second");
    check(empty.acquire(2)==rtos::Result::timeout,"empty semaphore timeout");
    check(empty.release()==rtos::Result::ok&&empty.release()==rtos::Result::overflow,"semaphore maximum");
    check(empty.acquire()==rtos::Result::ok,"semaphore conservation");
    uint32_t bits=0;
    check(phase.wait_any(0x80000000u,bits,2)==rtos::Result::timeout,"event timeout");
    check(owner_go.release()==rtos::Result::ok,"release owner");
    check(phase.wait_any(owner_locked,bits,100)==rtos::Result::ok,"owner ready");
    check(high_go.release()==rtos::Result::ok,"release high");
    check(medium_go.release()==rtos::Result::ok,"release medium");
    check(phase.wait_any(high_done,bits,200)==rtos::Result::ok,"inheritance completion");
    check(shared==2&&inherited.load()==1,"inheritance result");
    report("PASS: finite timeouts, FIFO limits, priority inheritance\n");
    check(phase.set(begin_stress)==rtos::Result::ok,"start workers");
    // A continuously ready CPU hog can consume a time slice after every IPC
    // scheduling point. Budget by work count, rather than assuming throughput.
    const auto completion=completed.wait_all(1|2|4|8|32,bits,rounds*8u+1000u);
    if(completion!=rtos::Result::ok){
        report("PROGRESS: produced=");number(produced);report(" consumed=");number(consumed);
        report(" done_bits=");number(completed.snapshot().bits);report(" ticks=");number(rtos::ticks());report("\n");
        fail("stress completion deadline");
    }
    stop_cpu.store(1);
    check(completed.wait_all(16,bits,200)==rtos::Result::ok,"CPU completion deadline");
    // Completion notification can preempt its sender before the entry returns.
    // Let each lower-priority worker finish through the real exit trampoline.
    check(rtos::sleep_for(1),"completion grace tick");
    const auto queue=fifo.snapshot();
    check(produced==2*rounds&&consumed==produced,"message totals");
    check(queue.depth==0&&!queue.send_waiters&&!queue.receive_waiters,"queue quiescence");
    check(ping.snapshot().count==1&&pong.snapshot().count==0,"token totals");
    check(!counts.snapshot().owner&&!inheritance.snapshot().owner,"mutex quiescence");
    check(cpu_rounds>0,"CPU load never ran");
    rtos::ThreadInfo rows[10];const auto info=rtos::snapshot(rows,10);
    check(info.required_threads==10&&info.written_threads==10&&info.ticks>=8,"thread/tick metrics");
    for(const auto& row:rows)check(row.guard_ok&&row.dispatches>0,"stack guard/dispatch");
    for(unsigned i=0;i<8;++i)check(rows[i].state==rtos::State::finished,"worker did not exit");
    rtos::StackUsage invalid{};invalid.used_bytes=123;
    check(!rtos::stack_usage(unregistered,invalid)&&invalid.used_bytes==123,"unknown stack identity");
    rtos::StackUsage zero{};
    check(rtos::stack_usage(*rows[0].id,zero,0)&&zero.watermark_enabled==bool(RTOS_STACK_WATERMARK)&&!zero.complete&&zero.inspected_bytes==0,"zero scan budget");
    check(rtos::stack_usage(*rows[0].id,zero,3)&&zero.inspected_bytes==0,"subword scan budget");
    for(unsigned i=0;i<10;++i){
        rtos::StackUsage usage{};
        check(rtos::stack_usage(*rows[i].id,usage,rows[i].stack_bytes),"stack observation identity");
        check(usage.guard_ok&&usage.capacity_bytes==rows[i].stack_bytes,"stack metadata");
#if RTOS_STACK_WATERMARK
        check(usage.watermark_enabled&&usage.complete,"stack observation completeness");
        check(usage.used_bytes>=72&&usage.used_bytes+usage.untouched_bytes==usage.capacity_bytes,"stack observation conservation");
#else
        check(!usage.watermark_enabled&&!usage.complete&&usage.used_bytes==0&&usage.untouched_bytes==0&&usage.inspected_bytes==0,"disabled stack metadata");
#endif
        report("STACK: id=");number(i);report(" used=");number(uint32_t(usage.used_bytes));
        report(" capacity=");number(uint32_t(usage.capacity_bytes));report(" enabled=");number(usage.watermark_enabled);report("\n");
    }
    rtos::StackUsage live{};
    check(rtos::stack_usage(threads[8],live,4)&&live.inspected_bytes<=4,"live short-budget observation");
    report("PASS: messages=");number(consumed);report(" token_handoffs=");number(2*rounds);
    report(" ticks=");number(info.ticks);report(" cpu_rounds=");number(cpu_rounds);report("\n");
    report("ARM_STRESS_PASS\n");finish(0);
}
}
/// Stress-only observer: frame slot16 is stacked PC after the software prefix.
extern "C" void rtos_arm_stress_tick(const uint32_t* frame,uint64_t elapsed){
    const auto pc=uintptr_t(frame[16]);
    if(pc>=reinterpret_cast<uintptr_t>(rtos_arm_irq_live_begin)&&
       pc<reinterpret_cast<uintptr_t>(rtos_arm_irq_live_end))++live_window_hits;
    if(catchup_pending){first_elapsed=uint32_t(elapsed);catchup_pending=false;}
}
/// Serialized timer observation validates preload, extension and monotonicity.
extern "C" void rtos_arm_stress_counter(uint64_t extended,uint32_t low){
    if(!counter_seen){initial_low=low;counter_seen=true;}
    else if(extended<previous_counter)counter_bad=true;
    if(extended>>32)counter_wrapped=true;
    previous_counter=extended;
}
/// All unexpected exceptions terminate the simulator with a distinct failure.
extern "C" [[noreturn]] void rtos_arm_fault(){fail("unexpected ARM exception/main return");}
int main(){
    constexpr rtos::Entry entries[]{producer,producer,consumer,token_worker,token_worker,low_owner,high_waiter,cpu_load,coordinator};
    constexpr uint8_t priorities[]{2,2,2,2,2,1,4,2,5};
    for(unsigned i=0;i<9;++i){
        void* argument=(i==1||i==4)?reinterpret_cast<void*>(uintptr_t(1)):nullptr;
        check(rtos::create(threads[i],entries[i],argument,priorities[i],stacks[i],"stress"),"thread registration");
    }
    rtos::StackUsage boot{};
    check(rtos::stack_usage(threads[0],boot,2048)&&boot.capacity_bytes==2048,"boot stack observation");
#if RTOS_STACK_WATERMARK
    check(boot.watermark_enabled&&boot.complete&&boot.used_bytes>=72,"boot frame paint");
#else
    check(!boot.watermark_enabled&&boot.used_bytes==0,"boot disabled paint");
#endif
    rtos::start(25000000,RTOS_TICK_HZ);
}
