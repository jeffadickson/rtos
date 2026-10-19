#include "sync_model.hpp"
#include "rtos_ipc.hpp"
using namespace rtos;
int main(){
    Thread thread;model::use(thread);model::scheduler.add(thread);
    EventFlags events;uint32_t observed=99;
    assert(events.wait_any(1,observed)==Result::empty&&observed==99);
    assert(events.set(7)==Result::ok);
    assert(events.wait_all(3,observed,NoWait,EventClear::yes)==Result::ok&&observed==3);
    assert(events.snapshot().bits==4);
    assert(events.wait_any(6,observed)==Result::ok&&observed==4&&events.snapshot().bits==4);
    assert(events.wait_any(4,observed,NoWait,EventClear::yes)==Result::ok&&events.snapshot().bits==0);
    assert(events.wait_all(0,observed)==Result::invalid);
    Queue<int,1> queue;assert(queue.send(42)==Result::ok);int value=0;assert(queue.receive(value)==Result::ok&&value==42);
    DeviceTransactions device;assert(device.transact(NoWait,[](){return Result::ok;})==Result::ok);
    AtomicU32 counter;assert(counter.fetch_add(1)==0&&counter.load()==1);
}