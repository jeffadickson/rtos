/** @file
 * @brief Reproducible single-CPU IPC state-machine checks against simple oracles.
 * Host model only: this does not simulate simultaneous CPU execution or DMA.
 */
#include "sync_model.hpp"
#include "rtos_queue.hpp"
#include "rtos_atomic.hpp"
#include <stdio.h>
#include <stdlib.h>
using namespace rtos;
namespace {
uint32_t seed,random_state,step,action;
size_t capacity_context;
Thread caller;
uint32_t next(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
void require(bool condition,const char* message){
    if(!condition){fprintf(stderr,"IPC_RANDOM_FAIL seed=%u capacity=%u state=%u step=%u action=%u: %s\n",
        seed,unsigned(capacity_context),random_state,step,action,message);abort();}
}
/// The reference FIFO shifts values; it shares no ring-index logic with Queue.
template<size_t Capacity> void campaign(){
    capacity_context=Capacity;
    Queue<uint32_t,Capacity> queue;
    Semaphore semaphore(0,7);EventFlags events;AtomicU32 atomic;
    uint32_t values[Capacity]{},tokens=0,event_bits=0,word=0;
    size_t depth=0,high_water=0;
    for(step=0;step<50000;++step){
        action=next()%18;const auto value=next();
        const uint32_t mask=((value>>8)&255u)|1u;
        model::isr=(value&0x10000u)!=0;
        switch(action){
        case 0:{
            const auto result=model::isr?queue.try_send_from_isr(value):queue.send(value);
            require(result==(depth==Capacity?Result::full:Result::ok),"send result");
            if(depth<Capacity){values[depth++]=value;if(depth>high_water)high_water=depth;}
            break;}
        case 1:{
            uint32_t output=0xdeadbeefu;
            const auto result=model::isr?queue.try_receive_from_isr(output):queue.receive(output);
            require(result==(depth?Result::ok:Result::empty),"receive result");
            if(depth){require(output==values[0],"FIFO order");--depth;for(size_t i=0;i<depth;++i)values[i]=values[i+1];}
            else require(output==0xdeadbeefu,"empty output preservation");
            break;}
        case 2:{
            const auto result=model::isr?semaphore.release_from_isr():semaphore.release();
            require(result==(tokens==7?Result::overflow:Result::ok),"token release");
            if(tokens<7)++tokens;
            break;}
        case 3:
            model::isr=false;
            require(semaphore.acquire()==(tokens?Result::ok:Result::empty),"token acquire");
            if(tokens)--tokens;
            break;
        case 4:
            require((model::isr?events.set_from_isr(mask):events.set(mask))==Result::ok,"event set");
            event_bits|=mask;break;
        case 5:
            model::isr=false;require(events.clear(mask)==Result::ok,"event clear");event_bits&=~mask;break;
        case 6:case 7:{
            model::isr=false;uint32_t observed=0xdeadbeefu;
            const uint32_t found=event_bits&mask;const bool all=action==7;
            const bool eligible=all?found==mask:found!=0;
            const bool consume=(value&1)!=0;
            const auto result=events.wait(mask,observed,0,all,consume);
            require(result==(eligible?Result::ok:Result::empty),"event wait result");
            require(observed==(eligible?found:0xdeadbeefu),"event observation");
            if(eligible&&consume)event_bits&=~found;
            break;}
        case 8:atomic.store(value);word=value;break;
        case 9:require(atomic.exchange(value)==word,"exchange prior");word=value;break;
        case 10:require(atomic.fetch_add(value)==word,"add prior");word+=value;break;
        case 11:require(atomic.fetch_or(value)==word,"or prior");word|=value;break;
        case 12:require(atomic.fetch_and(value)==word,"and prior");word&=value;break;
        case 13:{auto expected=word;require(atomic.compare_exchange(expected,value)&&expected==word,"CAS success");word=value;break;}
        case 14:{auto expected=word^1u;require(!atomic.compare_exchange(expected,value)&&expected==word,"CAS mismatch");break;}
        case 15:model::scheduler.advance(value&31u);break;
        case 16:{
            model::isr=false;uint32_t output=123;
            require(queue.receive(output,MaxWait+1)==Result::invalid&&output==123,"invalid timeout preservation");
            require(queue.send(value,MaxWait+1)==Result::invalid,"invalid send timeout");break;}
        case 17:
            model::isr=true;require(queue.send(value)==Result::context,"thread send in ISR");
            require(semaphore.acquire()==Result::context,"thread acquire in ISR");break;
        }
        model::isr=false;
        const auto info=queue.snapshot();
        require(info.depth==depth&&info.capacity==Capacity&&info.high_water==high_water,"queue metadata");
        require(info.send_waiters==0&&info.receive_waiters==0,"unexpected queue waiter");
        require(semaphore.snapshot().count==tokens&&events.snapshot().bits==event_bits,"IPC conservation");
        require(atomic.load()==word&&model::depth==0,"word state / critical balance");
    }
    printf("IPC_RANDOM_PASS seed=%u capacity=%u steps=%u\n",seed,unsigned(Capacity),step);
}
}
int main(int argc,char** argv){
    seed=argc==2?uint32_t(strtoul(argv[1],nullptr,10)):1u;
    if(!seed)return 2; // xorshift's zero state is intentionally excluded.
    random_state=seed;model::use(caller);require(model::scheduler.add(caller),"register caller");
    campaign<1>();campaign<3>();campaign<8>();
}
