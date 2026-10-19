#include "sync_model.hpp"
#include "rtos_mutex.hpp"
using namespace rtos;
static Thread a,b,c;
static Mutex first,second;
void release_first(){assert(a.effective_priority==9);model::use(a,1);assert(first.unlock()==Result::ok);}
void expire(){assert(a.effective_priority==9);model::scheduler.advance(3);assert(a.effective_priority==1);}
void inner_chain(){
    assert(a.effective_priority==9&&b.effective_priority==9);
    model::caller=&a;assert(first.unlock()==Result::ok);
    assert(a.effective_priority==1);model::caller=&b;assert(second.unlock()==Result::ok);
}
void chain(){
    model::use(c,9);model::on_park=inner_chain;assert(second.lock(8)==Result::ok);
    assert(second.snapshot().owner==&c);assert(second.unlock()==Result::ok);
}
void detect_cycle(){
    model::caller=&a;assert(second.lock(5)==Result::deadlock);
    assert(first.unlock()==Result::ok);
}
void abandon(){Mutex::owner_exit(a);}
int main(){
    model::use(a,1);model::scheduler.add(a);model::scheduler.add(b);model::scheduler.add(c);
    assert(first.lock()==Result::ok&&first.lock()==Result::deadlock);
    model::use(b,9);assert(first.unlock()==Result::context);assert(first.lock()==Result::busy);
    model::on_park=release_first;assert(first.lock(8)==Result::ok);assert(first.snapshot().owner==&b);assert(first.unlock()==Result::ok);
    model::use(a,1);assert(first.lock()==Result::ok);model::use(b,9);model::on_park=expire;
    assert(first.lock(3)==Result::timeout&&a.effective_priority==1);model::caller=&a;assert(first.unlock()==Result::ok);
    model::use(a,1);assert(first.lock()==Result::ok);model::use(b,2);assert(second.lock()==Result::ok);
    model::on_park=chain;assert(first.lock(9)==Result::ok);assert(first.unlock()==Result::ok);
    model::use(a,1);assert(first.lock()==Result::ok);model::use(b,2);assert(second.lock()==Result::ok);
    model::on_park=detect_cycle;assert(first.lock(9)==Result::ok);assert(first.unlock()==Result::ok);assert(second.unlock()==Result::ok);
    model::use(a,1);assert(first.lock()==Result::ok);model::use(b,9);model::on_park=abandon;
    assert(first.lock(8)==Result::owner_dead&&first.snapshot().owner_dead);assert(first.lock()==Result::owner_dead);
    assert(first.recover_after_quiescence()==Result::ok);assert(first.lock()==Result::ok);assert(first.unlock()==Result::ok);
    DeviceTransactions device;int calls=0;assert(device.transact(0,[&](){++calls;return Result::invalid;})==Result::invalid);
    assert(calls==1&&device.snapshot().owner==nullptr);
    model::isr=true;assert(first.lock()==Result::context);model::isr=false;
    assert(a.held_mutexes==0&&b.held_mutexes==0&&c.held_mutexes==0&&model::depth==0);
}
