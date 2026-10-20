/** @file
 * @brief Exhaustive three-waiter priority/FIFO and timeout-detachment contracts.
 * Recursive host parking publishes real wait nodes but does not execute ARM.
 */
#include "sync_model.hpp"
#include "rtos_queue.hpp"
using namespace rtos;
namespace {
Thread waiters[3],controller;
Semaphore semaphore(0,1);
Queue<uint32_t,3> queue;
constexpr unsigned permutations[6][3]{{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
unsigned order[3]{},priorities[3]{},enrolled{};
uint32_t values[3]{},expected_values[3]{};
bool use_queue{},expire_first{},change_priority{};
unsigned campaigns{};
void drive();
void enroll(){
    if(enrolled==3){drive();return;}
    const unsigned index=order[enrolled++];model::caller=&waiters[index];
    model::on_park=enroll;
    const bool expires=expire_first&&index==order[0];
    const auto result=use_queue?queue.receive(values[index],expires?2:10):semaphore.acquire(expires?2:10);
    assert(result==(expires?Result::timeout:Result::ok));
    assert(!waiters[index].wait.queue&&!waiters[index].wait.next&&!waiters[index].wait.data);
    if(use_queue)assert(values[index]==(expires?0xdeadbeefu:expected_values[index]));
}
void drive(){
    model::use(controller,10);
    assert((use_queue?queue.snapshot().receive_waiters:semaphore.snapshot().waiters)==3);
    if(change_priority){
        const auto changed=order[2];priorities[changed]=5;
        waiters[changed].priority=waiters[changed].effective_priority=5;
    }
    if(expire_first){
        model::scheduler.advance(2);
        assert(waiters[order[0]].state==State::ready&&waiters[order[0]].wait.result==Result::timeout);
    }
    // Independent stable insertion sort of enrollment IDs by requested priority.
    unsigned ranked[3]{},count=0;
    for(unsigned position=0;position<3;++position){
        const auto id=order[position];if(expire_first&&position==0)continue;
        unsigned at=count;
        while(at&&priorities[ranked[at-1]]<priorities[id]){ranked[at]=ranked[at-1];--at;}
        ranked[at]=id;++count;
    }
    for(unsigned rank=0;rank<count;++rank){
        const auto chosen=ranked[rank];expected_values[chosen]=100+rank;
        assert((use_queue?queue.send(100+rank):semaphore.release())==Result::ok);
        assert(waiters[chosen].state==State::ready&&!waiters[chosen].wait.queue&&!waiters[chosen].wait.data);
        for(unsigned later=rank+1;later<count;++later)assert(waiters[ranked[later]].state==State::blocked);
    }
    assert((use_queue?queue.snapshot().receive_waiters:semaphore.snapshot().waiters)==0);
    // A late supply cannot target a timed-out stack payload or stale node.
    if(use_queue){uint32_t output=0;assert(queue.send(999)==Result::ok&&queue.receive(output)==Result::ok&&output==999);}
    else assert(semaphore.release()==Result::ok&&semaphore.acquire()==Result::ok);
}
}
int main(){
    for(auto& waiter:waiters)assert(model::scheduler.add(waiter));
    assert(model::scheduler.add(controller));
    for(unsigned mode=0;mode<2;++mode)for(unsigned permutation=0;permutation<6;++permutation)
    for(unsigned assignment=0;assignment<27;++assignment)for(unsigned variant=0;variant<4;++variant){
        use_queue=mode!=0;expire_first=(variant&1)!=0;change_priority=(variant&2)!=0;
        model::scheduler.now=UINT32_MAX-1;enrolled=0;unsigned code=assignment;
        for(unsigned i=0;i<3;++i){
            order[i]=permutations[permutation][i];priorities[i]=code%3;code/=3;
            model::use(waiters[i],uint8_t(priorities[i]));values[i]=0xdeadbeefu;
        }
        enroll();assert(model::depth==0);++campaigns;
    }
    assert(campaigns==1296);
}
