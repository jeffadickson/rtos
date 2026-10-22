/** @file
 * @brief Seeded scheduler lifecycle checks using an unwrapped reference clock.
 * Host state transitions only; no target context switches or simultaneous CPUs.
 */
#include "sync_model.hpp"
#include <stdio.h>
#include <stdlib.h>
using namespace rtos;
namespace {
constexpr unsigned count=detail::Scheduler::capacity<8?detail::Scheduler::capacity:8;
uint32_t seed=1,random_state,step,round_number;
uint32_t next(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
void require(bool condition,const char* message){
    if(!condition){fprintf(stderr,"THREAD_STRESS_FAIL seed=%u state=%u round=%u step=%u: %s\n",
        seed,random_state,round_number,step,message);abort();}
}
struct Reference { State state{State::ready};uint64_t wake{};uint8_t priority{}; };
void campaign(){
    detail::Scheduler scheduler;
    Thread threads[count];Reference expected[count];
    uint64_t clock=uint64_t(UINT32_MAX)-128;
    scheduler.now=Tick(clock);
    for(unsigned i=0;i<count;++i){
        threads[i].state=State::ready;threads[i].priority=threads[i].effective_priority=0;
        require(scheduler.add(threads[i]),"registration");
        require(!scheduler.add(threads[i]),"duplicate registration");
    }
    Thread outsider;outsider.state=State::ready;
    Thread* previous=&outsider;
    for(step=0;step<512;++step){
        const auto index=next()%count,action=next()%6;
        auto& thread=threads[index];auto& reference=expected[index];
        if(action==0&&reference.state!=State::finished){
            const auto delay=1+next()%100;
            reference.state=thread.state=State::sleeping;reference.wake=clock+delay;thread.wake=Tick(reference.wake);
        }else if(action==1&&reference.state!=State::finished){reference.state=thread.state=State::ready;}
        else if(action==2){
            reference.priority=uint8_t(next()%6);thread.priority=thread.effective_priority=reference.priority;
        }else if(action==3){
            const auto elapsed=next()%33;clock+=elapsed;
            for(auto& item:expected)if(item.state==State::sleeping&&clock>=item.wake)item.state=State::ready;
            scheduler.advance(elapsed);
        }else if(action==4&&step>=480){reference.state=thread.state=State::finished;}
        for(unsigned i=0;i<count;++i)require(threads[i].state==expected[i].state,"wake/lifecycle state");
        require(scheduler.now==Tick(clock),"wrapped clock");
        const bool rotate=(next()&1)!=0;
        bool any=false;uint8_t highest=0;
        for(const auto& item:expected)if(item.state==State::ready){any=true;if(item.priority>highest)highest=item.priority;}
        const auto selected=scheduler.pick(previous,rotate);
        require(bool(selected)==any,"runnable availability");
        if(selected){
            unsigned chosen=count;
            for(unsigned i=0;i<count;++i)if(selected==&threads[i])chosen=i;
            require(chosen<count,"registered selection");
            require(expected[chosen].state==State::ready&&expected[chosen].priority==highest,"highest ready priority");
        }
        if(!rotate&&previous!=&outsider&&previous&&previous->state==State::ready&&previous->effective_priority==highest)
            require(selected==previous,"nonrotating selection retains eligible thread");
        previous=selected;
        require(model::depth==0,"critical balance");
    }
    require(clock>UINT32_MAX,"campaign crossed tick wrap");
    for(auto& thread:threads)thread.state=State::finished;
    scheduler.advance(1000);require(scheduler.pick()==nullptr,"finished threads stay retired");
    // A separate stable cohort avoids reviving retired control blocks.
    detail::Scheduler fair;Thread peers[count];
    for(auto& thread:peers){
        thread.state=State::ready;thread.priority=thread.effective_priority=3;
        require(fair.add(thread),"fairness registration");
    }
    for(unsigned cycle=0;cycle<16;++cycle){
        bool seen[count]{};
        for(unsigned slot=0;slot<count;++slot){
            auto* selected=fair.pick();unsigned id=count;
            for(unsigned i=0;i<count;++i)if(selected==&peers[i])id=i;
            require(id<count,"fairness selection registered");require(!seen[id],"round-robin duplicate");seen[id]=true;
        }
    }
}
}
int main(int argc,char** argv){
    if(argc>1)seed=uint32_t(strtoul(argv[1],nullptr,0));
    if(!seed)seed=1;
    random_state=seed;
    for(round_number=0;round_number<128;++round_number)campaign();
    printf("THREAD_STRESS_PASS seed=%u rounds=128 steps=65536 threads=%u\n",seed,count);
}
