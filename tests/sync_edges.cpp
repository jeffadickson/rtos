#include "sync_model.hpp"
#include "rtos_mutex.hpp"
#include "rtos_queue.hpp"
using namespace rtos;
static Thread a,b,c;
static EventFlags events;
static Semaphore semaphore(0,2);
static Mutex mutex;
static uint32_t nonclearing{},consuming{};
void set_event(){model::caller=&c;assert(events.set(1)==Result::ok);}
void consume_event(){model::use(b,9);model::on_park=set_event;assert(events.wait(1,consuming,4,false,true)==Result::ok);}
void release_two(){
    model::caller=&c;assert(semaphore.release()==Result::ok);
    assert(a.state==State::ready&&b.state==State::blocked);
    assert(semaphore.release()==Result::ok&&b.state==State::ready);
}
void second_waiter(){model::use(b,5);model::on_park=release_two;assert(semaphore.acquire(4)==Result::ok);}
void donation_change(){
    assert(a.effective_priority==9);b.priority=12;Mutex::priority_changed(b);assert(a.effective_priority==12);
    b.priority=2;Mutex::priority_changed(b);assert(a.effective_priority==2);
    model::scheduler.advance(4);assert(a.effective_priority==1);
}
int main(){
    model::use(a,1);model::scheduler.add(a);model::scheduler.add(b);model::scheduler.add(c);
    model::on_park=consume_event;assert(events.wait(1,nonclearing,4)==Result::ok&&nonclearing==1&&consuming==1);
    assert(events.snapshot().bits==0);
    model::use(a,5);model::on_park=second_waiter;assert(semaphore.acquire(4)==Result::ok&&semaphore.snapshot().count==0);
    model::use(a,1);assert(mutex.lock()==Result::ok);model::use(b,9);model::on_park=donation_change;
    assert(mutex.lock(4)==Result::timeout);model::caller=&a;assert(mutex.unlock()==Result::ok);
    Mutex held[RTOS_MAX_HELD_MUTEXES+1];
    for(unsigned i=0;i<RTOS_MAX_HELD_MUTEXES;++i)assert(held[i].lock()==Result::ok);
    assert(held[RTOS_MAX_HELD_MUTEXES].lock()==Result::overflow);
    for(unsigned i=0;i<RTOS_MAX_HELD_MUTEXES;++i)assert(held[i].unlock()==Result::ok);
    assert(a.held_mutexes==0&&model::depth==0);
}
