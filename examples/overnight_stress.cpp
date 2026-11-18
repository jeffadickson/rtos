/** @file
 * @brief Seeded, boot-static real-kernel soak workload. Building never starts it.
 * Debugger-visible status is a reporting channel, not a hardware watchdog.
 */
#include "rtos_queue.hpp"
#include "rtos_mutex.hpp"
#include "rtos_atomic.hpp"
#ifndef RTOS_SOAK_SEED
#define RTOS_SOAK_SEED 0x4e585254u
#endif
#ifndef RTOS_SOAK_HOURS
#define RTOS_SOAK_HOURS 12
#endif
#ifndef RTOS_SOAK_CLOCK_HZ
#define RTOS_SOAK_CLOCK_HZ 10000000u
#endif
static_assert(RTOS_SOAK_SEED!=0,"xorshift32 needs a nonzero seed");
static_assert(RTOS_SOAK_HOURS>=1&&RTOS_SOAK_HOURS<=72);
static_assert(RTOS_TICK_HZ>=100&&RTOS_TICK_HZ<=10000);
static_assert(uint64_t(RTOS_SOAK_HOURS)*3600*RTOS_TICK_HZ<=rtos::MaxWait);
static_assert(RTOS_MAX_THREADS>=6);
static_assert(RTOS_METRICS&&RTOS_STACK_GUARD&&RTOS_STACK_WATERMARK&&RTOS_ROUND_ROBIN);

/// Sole writer is the supervisor, except boot initialization before scheduling.
/// Debugger reads may be torn while running; phase 2/3 is the final record.
struct SoakStatus {
    uint32_t phase,seed,failure,failed_worker,failed_sequence;
    uint32_t elapsed_ticks,completed,mode_counts[3],worker_jobs[4],receive_timeouts[4];
    uint32_t busy_percent,dispatches[6],runtime_low[6],stack_used[6];
    uint32_t states[6],priorities[6],effective_priorities[6];
    uint32_t expected_total,observed_total;
};
volatile SoakStatus nexrt_soak_status{};
/// Retained ELF identity checked by the simulation launcher against its 100 MHz timer.
extern const volatile uint32_t nexrt_soak_clock_hz=RTOS_SOAK_CLOCK_HZ;
static_assert(sizeof(SoakStatus)==57*4,"Update the simulator status reader with this ABI");
namespace {
constexpr unsigned workers=4;
constexpr rtos::Tick second=RTOS_TICK_HZ;
constexpr rtos::Tick duration=rtos::Tick(uint64_t(RTOS_SOAK_HOURS)*3600*second);
constexpr uint32_t stop_mode=3;
struct Job {uint32_t sequence,seed,mode,steps,pause,priority;};
struct Completion {uint32_t worker;Job job;uint32_t value;};
struct Pending {Job job{};uint32_t value{};};
rtos::Thread threads[6];
rtos::Stack<1024> worker_stacks[workers];
rtos::Stack<1536> control_stacks[2];
constexpr const char* names[]{"soak-cpu0","soak-cpu1","soak-memory","soak-mixed","soak-dispatch","soak-watch"};
unsigned identities[]{0,1,2,3};
rtos::Queue<Job,1> inbox[workers];
rtos::Queue<Completion,8> completed;
rtos::Mutex totals_mutex;
rtos::AtomicU32 available(15),stopping,dispatcher_done,failure,failed_worker,failed_sequence;
rtos::AtomicU32 timeouts[workers];
rtos::AtomicU32 heartbeat[5];
Pending pending[workers]; // Owned by dispatcher until send, supervisor until slot release.
uint32_t shared_total;
rtos::ThreadInfo observations[7];
rtos::Tick started;

/// Tiny deterministic PRNG on the target: no library, entropy device or heap.
uint32_t random(uint32_t& state){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
[[gnu::noinline]] void record_failure(uint32_t code,uint32_t worker=UINT32_MAX,uint32_t sequence=0){
    // Claim the first failure, then publish details before its final code.
    uint32_t empty=0;
    if(failure.compare_exchange(empty,UINT32_MAX)){
        failed_worker.store(worker);failed_sequence.store(sequence);failure.store(code);
    }
    stopping.store(1);
}
bool same(const Job& a,const Job& b){
    return a.sequence==b.sequence&&a.seed==b.seed&&a.mode==b.mode&&a.steps==b.steps&&a.pause==b.pause&&a.priority==b.priority;
}
/// Dependent integer work remains live at -O3 because its checksum is validated.
/// Replay detects corruption; it is not an independent CPU-instruction oracle.
[[gnu::noinline]] uint32_t compute(const Job& job,bool cooperate){
    uint32_t state=job.seed,a=0x12345678u,b=0x9abcdef0u;
    if(job.mode==1){
        uint32_t scratch[32];
        for(uint32_t round=0;round<job.steps/32+1;++round){
            for(unsigned i=0;i<32;++i)scratch[i]=random(state)^uint32_t(i+round);
            for(unsigned i=32;i>0;--i){a=(a<<5)|(a>>27);a^=scratch[i-1];}
            if(cooperate&&(round&7)==0)rtos::yield();
        }
    }else{
        for(uint32_t i=0;i<job.steps;++i){
            a+=random(state)^i;b=(b<<7)|(b>>25);b^=a;
            if(cooperate&&job.mode==2&&(i&255)==0)rtos::yield();
        }
    }
    return a^b^state;
}
void worker(void* argument){
    const auto id=*static_cast<unsigned*>(argument);
    uint32_t local=RTOS_SOAK_SEED^(0x9e3779b9u*(id+1));if(!local)local=1;
    for(;;){
        heartbeat[id].store(rtos::ticks());
        Job job{};const auto result=inbox[id].receive(job,1+random(local)%(second/4));
        if(result==rtos::Result::timeout){timeouts[id].fetch_add(1);continue;}
        if(result!=rtos::Result::ok){record_failure(1,id);return;}
        if(job.mode==stop_mode){
            if(completed.send({id,job,0},10*second)!=rtos::Result::ok)record_failure(2,id);
            return;
        }
        const auto value=compute(job,true);
        if(totals_mutex.lock(10*second)!=rtos::Result::ok){record_failure(3,id,job.sequence);return;}
        // Occasionally block while owning the mutex to exercise priority donation.
        if((job.seed&15)==0)(void)rtos::sleep_for(1);
        shared_total+=value;
        if(totals_mutex.unlock()!=rtos::Result::ok){record_failure(4,id,job.sequence);return;}
        if(completed.send({id,job,value},10*second)!=rtos::Result::ok){record_failure(5,id,job.sequence);return;}
        (void)rtos::sleep_for(job.pause);
    }
}
void dispatch(void*){
    uint32_t state=RTOS_SOAK_SEED,sequences[workers]{};
    while(!stopping.load()){
        heartbeat[4].store(rtos::ticks());
        const auto id=random(state)%workers,mask=uint32_t(1)<<id;
        if(available.fetch_and(~mask)&mask){
            Job job{++sequences[id],random(state),random(state)%3,64+random(state)%4033,
                    1+random(state)%(second/8),1+random(state)%6};
            if(!job.seed)job.seed=1;
            pending[id]={job,compute(job,false)};
            if(!rtos::set_priority(threads[id],uint8_t(job.priority))){record_failure(6,id,job.sequence);return;}
            if(inbox[id].send(job,10*second)!=rtos::Result::ok){record_failure(7,id,job.sequence);return;}
        }
        (void)rtos::sleep_for(1+random(state)%(second/100+1));
    }
    // FIFO stop messages follow any already accepted work; workers really exit here.
    for(unsigned id=0;id<workers;++id){
        Job stop{};stop.mode=stop_mode;
        if(inbox[id].send(stop,10*second)!=rtos::Result::ok){record_failure(8,id);return;}
    }
    dispatcher_done.store(1);
}
void diagnostics(uint64_t& previous_elapsed,uint64_t& previous_idle){
    const auto system=rtos::snapshot(observations,7);
    if(system.written_threads!=7||!system.metrics_enabled){record_failure(9);return;}
    const auto idle=observations[6].runtime_cycles;
    const auto elapsed=system.elapsed_cycles-previous_elapsed,idle_delta=idle-previous_idle;
    if(elapsed)nexrt_soak_status.busy_percent=uint32_t(100-(idle_delta>elapsed?elapsed:idle_delta)*100/elapsed);
    previous_elapsed=system.elapsed_cycles;previous_idle=idle;
    for(unsigned i=0;i<6;++i){
        if(observations[i].id!=&threads[i]||!observations[i].guard_ok){record_failure(10,i);return;}
        nexrt_soak_status.dispatches[i]=uint32_t(observations[i].dispatches);
        nexrt_soak_status.runtime_low[i]=uint32_t(observations[i].runtime_cycles);
        nexrt_soak_status.states[i]=uint32_t(observations[i].state);
        nexrt_soak_status.priorities[i]=observations[i].priority;
        nexrt_soak_status.effective_priorities[i]=observations[i].effective_priority;
        rtos::StackUsage usage{};
        if(!rtos::stack_usage(threads[i],usage,128)||!usage.guard_ok){record_failure(11,i);return;}
        nexrt_soak_status.stack_used[i]=uint32_t(usage.used_bytes); // Bounded paint estimate.
    }
    if(!observations[6].guard_ok)record_failure(12,6);
    for(unsigned i=0;i<workers;++i)nexrt_soak_status.receive_timeouts[i]=timeouts[i].load();
}
void watch(void*){
    uint32_t expected_total=0,sequences[workers]{},retired=0;
    auto last_result=rtos::ticks(),last_report=last_result,stop_time=last_result;
    uint64_t previous_elapsed=0,previous_idle=0;
    for(;;){
        Completion result{};const auto received=completed.receive(result,second);
        const auto now=rtos::ticks();
        if(received==rtos::Result::ok){
            last_result=now;
            if(result.worker>=workers){record_failure(13);}
            else if(result.job.mode==stop_mode){
                const auto mask=uint32_t(1)<<result.worker;
                if(retired&mask)record_failure(14,result.worker);
                retired|=mask;
            }else{
                const auto id=result.worker;const auto& expected=pending[id];
                if(!same(result.job,expected.job)||result.value!=expected.value||result.job.sequence!=sequences[id]+1)
                    record_failure(15,id,result.job.sequence);
                else{
                    sequences[id]=result.job.sequence;expected_total+=result.value;
                    nexrt_soak_status.completed=nexrt_soak_status.completed+1;
                    nexrt_soak_status.worker_jobs[id]=nexrt_soak_status.worker_jobs[id]+1;
                    nexrt_soak_status.mode_counts[result.job.mode]=nexrt_soak_status.mode_counts[result.job.mode]+1;
                }
                available.fetch_or(uint32_t(1)<<id);
            }
        }else if(received!=rtos::Result::timeout)record_failure(16);
        nexrt_soak_status.elapsed_ticks=now-started;
        if(now-last_report>=second/10){diagnostics(previous_elapsed,previous_idle);last_report=now;}
        if(!stopping.load())for(unsigned i=0;i<5;++i){
            const auto beat=heartbeat[i].load();
            if(rtos::ticks()-beat>30*second)record_failure(22,i);
        }
        if(!stopping.load()&&now-last_result>30*second)record_failure(17);
        if(!stopping.load()&&now-started>=duration){stop_time=now;stopping.store(1);}
        const auto code=failure.load();
        if(code&&code!=UINT32_MAX){
            nexrt_soak_status.failed_worker=failed_worker.load();nexrt_soak_status.failed_sequence=failed_sequence.load();
            nexrt_soak_status.failure=code;nexrt_soak_status.phase=3;
            for(;;)(void)rtos::sleep_for(second);
        }
        if(stopping.load()&&dispatcher_done.load()&&retired==15){
            (void)rtos::snapshot(observations,7);
            bool finished=true;for(unsigned i=0;i<5;++i)if(observations[i].state!=rtos::State::finished)finished=false;
            if(finished){
                if(available.load()!=15){record_failure(23);continue;}
                nexrt_soak_status.expected_total=expected_total;nexrt_soak_status.observed_total=shared_total;
                if(shared_total!=expected_total){record_failure(18);continue;}
                for(unsigned i=0;i<3;++i)if(!nexrt_soak_status.mode_counts[i])record_failure(19);
                if(failure.load())continue;
                diagnostics(previous_elapsed,previous_idle);if(failure.load())continue;
                nexrt_soak_status.phase=2;return;
            }
            (void)rtos::sleep_for(1); // Let stop senders return and retire.
        }
        if(stopping.load()&&now-stop_time>30*second)record_failure(20);
    }
}
}
extern "C" int main(){
    nexrt_soak_status.seed=RTOS_SOAK_SEED;
    for(unsigned i=0;i<6;++i){
        const auto entry=i<workers?worker:(i==4?dispatch:watch);
        void* stack=i<workers?static_cast<void*>(worker_stacks[i].words):static_cast<void*>(control_stacks[i-workers].words);
        const auto bytes=i<workers?sizeof(worker_stacks[0]):sizeof(control_stacks[0]);
        if(!rtos::create(threads[i],entry,i<workers?&identities[i]:nullptr,i==5?8:(i==4?2:3),stack,bytes,names[i])){
            nexrt_soak_status.failure=21;nexrt_soak_status.phase=3;return 1;
        }
    }
    started=rtos::ticks();nexrt_soak_status.phase=1;
    rtos::start(nexrt_soak_clock_hz);
}
