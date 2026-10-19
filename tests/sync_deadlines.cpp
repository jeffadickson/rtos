#include "sync_model.hpp"
#include "rtos_queue.hpp"
#include "rtos_mutex.hpp"
using namespace rtos;
static Thread a,b,c;
static Semaphore signal(0,1);
static EventFlags flags;
static Queue<int,1> queue;
static Mutex mutex;
static uint32_t observer_bits{},consumer_bits{};
static void tick_then_signal(){
    model::scheduler.advance(1);model::use(c);
    assert(signal.release()==Result::ok&&signal.snapshot().count==1);
}
static void signal_then_tick(){
    model::use(c);assert(signal.release()==Result::ok);model::scheduler.advance(1);
    assert(signal.snapshot().count==0&&a.wait.queue==nullptr&&a.wait.data==nullptr);
}
static void wrap_timeout(){model::scheduler.advance(1);}
static void maximum_timeout(){model::scheduler.advance(MaxWait);}
static void receive_after_timeout(){
    model::use(c);model::scheduler.advance(2);
    assert(a.state==State::ready&&a.wait.queue==nullptr&&a.wait.data==nullptr);
    assert(b.state==State::blocked&&queue.snapshot().send_waiters==1);
    int value=-1;assert(queue.receive(value)==Result::ok&&value==1);
    assert(b.state==State::ready&&b.wait.queue==nullptr&&b.wait.data==nullptr);
}
static void second_sender(){
    model::use(b,9);model::on_park=receive_after_timeout;
    assert(queue.send(20,4)==Result::ok);
    int value=-1;assert(queue.receive(value)==Result::ok&&value==20);
}
static void publish_flags(){model::use(c);assert(flags.set(3)==Result::ok);}
static void consuming_waiter(){
    model::use(b,9);model::on_park=publish_flags;
    assert(flags.wait_any(1,consumer_bits,4,EventClear::yes)==Result::ok&&consumer_bits==1);
}
static void abandon_owner(){
    model::caller=&a;assert(a.effective_priority==9);
    Mutex::owner_exit(a);
    a.state=State::finished;
    assert(!a.owned_mutexes&&a.held_mutexes==0&&a.effective_priority==a.priority);
    assert(mutex.snapshot().owner_dead&&!mutex.snapshot().owner&&mutex.snapshot().waiters==0);
}
static void second_mutex_waiter(){
    model::use(c,9);model::on_park=abandon_owner;
    assert(mutex.lock(5)==Result::owner_dead);
}
int main(){
    model::use(a);model::scheduler.add(a);model::scheduler.add(b);model::scheduler.add(c);
    model::on_park=tick_then_signal;assert(signal.acquire(1)==Result::timeout);
    assert(signal.acquire()==Result::ok);
    model::on_park=signal_then_tick;assert(signal.acquire(1)==Result::ok);
    assert(signal.acquire()==Result::empty);
    model::scheduler.now=UINT32_MAX;model::on_park=wrap_timeout;
    assert(signal.acquire(1)==Result::timeout&&model::scheduler.now==0);
    uint32_t observed=0xa5;
    model::on_park=maximum_timeout;
    assert(flags.wait_all(3,observed,MaxWait)==Result::timeout&&observed==0xa5);
    assert(!flags.snapshot().waiters&&a.wait.queue==nullptr);
    assert(queue.send(1)==Result::ok);model::on_park=second_sender;
    assert(queue.send(10,2)==Result::timeout);
    auto info=queue.snapshot();assert(info.depth==0&&info.timeouts==1&&info.send_waiters==0);
    int value=77;assert(queue.receive(value)==Result::empty&&value==77);
    model::use(a,1);model::on_park=consuming_waiter;
    assert(flags.wait_all(3,observer_bits,4)==Result::ok&&observer_bits==3);
    assert(flags.snapshot().bits==2&&flags.snapshot().waiters==0);
    assert(flags.clear(2)==Result::ok);
    assert(mutex.lock()==Result::ok);model::use(b,5);model::on_park=second_mutex_waiter;
    assert(mutex.lock(5)==Result::owner_dead);
    assert(b.wait.queue==nullptr&&c.wait.queue==nullptr&&b.waiting_mutex==nullptr&&c.waiting_mutex==nullptr);
    assert(mutex.recover_after_quiescence()==Result::ok&&mutex.lock()==Result::ok&&mutex.unlock()==Result::ok);
    uint32_t counter=UINT32_MAX-1;detail::increment(counter);detail::increment(counter);assert(counter==UINT32_MAX);
    assert(a.held_mutexes==0&&b.held_mutexes==0&&c.held_mutexes==0&&model::depth==0);
}
