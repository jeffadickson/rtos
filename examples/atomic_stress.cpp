/** @file Matched real-kernel atomic fixture for baseline and native-RAM Zaamo builds. */
#include "rtos_atomic.hpp"
#if RTOS_ZAAMO
#include "rtos_hal_atomic.hpp"
#endif
struct AtomicStatus {
    uint32_t phase,feature,failure,workers,total,flags,ticket_words,exchange_ok,cas_checks,guards,clock_hz,rounds;
};
volatile AtomicStatus nexrt_atomic_status{};
extern const volatile uint32_t nexrt_atomic_clock_hz=100000000;
static_assert(sizeof(AtomicStatus)==12*4);
namespace {
constexpr unsigned worker_count=3,rounds=64,total=worker_count*rounds;
rtos::Thread threads[4];
rtos::Stack<1024> stacks[4];
unsigned ids[]{0,1,2};
rtos::AtomicU32 counter,flags,swapped,done,failure,tickets[total/32];
struct Row {uint32_t old_sum{},write_sum{},cas_checks{},jobs{};};
Row rows[worker_count];
rtos::ThreadInfo observations[5];
/// Preserve the first failure even if another worker later notices fallout.
void fail(uint32_t code){uint32_t empty=0;(void)failure.compare_exchange(empty,code);}
/// Check each operation's return value, wrapping arithmetic and both CAS branches before scheduling.
bool self_test(){
    rtos::AtomicU32 word(UINT32_MAX);
    if(word.fetch_add(1)!=UINT32_MAX||word.load()!=0)return false;
    word.store(6);if(word.exchange(9)!=6)return false;
    if(word.fetch_or(16)!=9||word.fetch_and(15)!=25||word.load()!=9)return false;
    uint32_t expected=9;
    if(!word.compare_exchange(expected,3)||expected!=9)return false;
    expected=9;
    return !word.compare_exchange(expected,7)&&expected==3&&word.load()==3;
}
/** @brief Exercise shared RMWs while yields/timer sleeps let peers interleave.
 * Each returned increment value claims one unique ticket. Each worker alone
 * owns its bit in flags, while peers concurrently change different bits. The
 * exchange sums are private to each worker and reconciled after retirement.
 * local is deliberately private: CAS remains single-CPU software serialization.
 */
void worker(void* pointer){
    const auto id=*static_cast<unsigned*>(pointer);
    const uint32_t bit=uint32_t(1)<<id;
    rtos::AtomicU32 local;
    for(unsigned i=0;i<rounds;++i){
        const auto ticket=counter.fetch_add(1);
        if(ticket>=total){fail(1);break;}
        const auto mask=uint32_t(1)<<(ticket%32);
        if(tickets[ticket/32].fetch_or(mask)&mask){fail(2);break;}
        if(flags.fetch_or(bit)&bit){fail(3);break;}
        rtos::yield(); // Peers modify different bits before this worker clears its own.
        if(!(flags.fetch_and(~bit)&bit)){fail(4);break;}
        const auto value=id+1;
        const auto old=swapped.exchange(value);
        if(old>worker_count){fail(5);break;}
        rows[id].old_sum+=old;rows[id].write_sum+=value;
        uint32_t expected=i;
        if(!local.compare_exchange(expected,i+1)){fail(6);break;}
        ++rows[id].cas_checks;expected=i;
        if(local.compare_exchange(expected,0)||expected!=i+1){fail(7);break;}
        ++rows[id].cas_checks;++rows[id].jobs;
        if(i%4==0)(void)rtos::sleep_for(1);
    }
    done.fetch_or(bit); // The observer waits for actual entry return/retirement too.
}
/** @brief Wait for actual thread retirement, then check independent invariants.
 * The done bitmap alone is insufficient: workers publish it before returning.
 * Ticket coverage detects missing/duplicate increments; exchange telescoping
 * checks old-value conservation; guard observations detect sentinel corruption.
 * The five-second limit uses target ticks, not simulator host wall time.
 */
void observe(void*){
    const auto start=rtos::ticks();
    while(done.load()!=7){
        if(failure.load()||rtos::ticks()-start>5*RTOS_TICK_HZ)break;
        (void)rtos::sleep_for(1);
    }
    for(;;){
        const auto snapshot=rtos::snapshot(observations,5);
        if(snapshot.written_threads!=5){fail(8);break;}
        bool finished=true;for(unsigned i=0;i<worker_count;++i)
            if(observations[i].state!=rtos::State::finished)finished=false;
        if(finished)break;
        if(failure.load()||rtos::ticks()-start>5*RTOS_TICK_HZ){fail(9);break;}
        (void)rtos::sleep_for(1);
    }
    uint32_t old_sum=0,write_sum=0,checks=0,guards=0,complete_words=0;
    for(unsigned i=0;i<worker_count;++i){
        old_sum+=rows[i].old_sum;write_sum+=rows[i].write_sum;checks+=rows[i].cas_checks;
        if(rows[i].jobs!=rounds)fail(10);
    }
    for(auto& word:tickets)if(word.load()==UINT32_MAX)++complete_words;
    for(const auto& observation:observations)if(observation.guard_ok)++guards;
    const bool exchange_ok=old_sum+swapped.load()==write_sum;
    if(counter.load()!=total||flags.load()!=0||complete_words!=total/32||!exchange_ok||guards!=5)fail(11);
    nexrt_atomic_status.workers=done.load();nexrt_atomic_status.total=counter.load();
    nexrt_atomic_status.flags=flags.load();nexrt_atomic_status.ticket_words=complete_words;
    nexrt_atomic_status.exchange_ok=exchange_ok;nexrt_atomic_status.cas_checks=checks;
    nexrt_atomic_status.guards=guards;nexrt_atomic_status.failure=failure.load();
    nexrt_atomic_status.phase=failure.load()?3:2;
}
}
/// Publish the build identity, validate optional hardware before using it, register static threads and start.
extern "C" int main(){
    nexrt_atomic_status.feature=RTOS_ZAAMO;nexrt_atomic_status.clock_hz=nexrt_atomic_clock_hz;
    nexrt_atomic_status.rounds=rounds;
#if RTOS_ZAAMO
    if(!rtos::hal::atomic::available()){nexrt_atomic_status.failure=12;nexrt_atomic_status.phase=3;return 1;}
#endif
    if(!self_test()){nexrt_atomic_status.failure=13;nexrt_atomic_status.phase=3;return 1;}
    for(unsigned i=0;i<4;++i){
        if(!rtos::create(threads[i],i<3?worker:observe,i<3?&ids[i]:nullptr,i<3?2:8,stacks[i],"atomic-test")){
            nexrt_atomic_status.failure=14;nexrt_atomic_status.phase=3;return 1;
        }
    }
    nexrt_atomic_status.phase=1;rtos::start(nexrt_atomic_clock_hz);
}
