#include "sync_model.hpp"
#include "rtos_queue.hpp"
#include "rtos_atomic.hpp"
using namespace rtos;
static Queue<int,2> q;
static Thread a,b,c;
static int low_value{},high_value{};
void remove_one(){model::use(b);int value=0;assert(q.receive(value)==Result::ok&&value==1);}
void send_one(){model::use(b);assert(q.send(17)==Result::ok);}
void timeout(){model::scheduler.advance(3);}
void finish_two(){model::use(c);assert(q.send(30)==Result::ok);}
void priority_wait(){
    model::use(b,9);model::on_park=finish_two;
    assert(q.receive(high_value,8)==Result::ok&&high_value==30);
    model::use(c);assert(q.send(40)==Result::ok);
}
int main(){
    model::use(a,1);assert(model::scheduler.add(a));assert(model::scheduler.add(b));assert(model::scheduler.add(c));
    int value=99;assert(q.receive(value)==Result::empty&&value==99);
    assert(q.send(1)==Result::ok&&q.send(2)==Result::ok&&q.send(3)==Result::full);
    model::on_park=remove_one;assert(q.send(3,5)==Result::ok);
    assert(q.receive(value)==Result::ok&&value==2);assert(q.receive(value)==Result::ok&&value==3);
    model::on_park=send_one;assert(q.receive(value,5)==Result::ok&&value==17);
    model::on_park=timeout;value=99;assert(q.receive(value,3)==Result::timeout&&value==99);
    assert(q.send(1)==Result::ok&&q.send(2)==Result::ok);assert(q.send(3,3)==Result::timeout);
    assert(q.receive(value)==Result::ok&&q.receive(value)==Result::ok);
    model::on_park=priority_wait;assert(q.receive(low_value,8)==Result::ok&&low_value==40);
    auto info=q.snapshot();assert(info.depth==0&&info.high_water==2&&info.timeouts==2&&info.send_waiters==0&&info.receive_waiters==0);
    assert(q.receive(value,0x80000000u)==Result::invalid);
    assert(q.try_send_from_isr(4)==Result::context);model::isr=true;
    assert(q.send(4)==Result::context);assert(q.try_send_from_isr(4)==Result::ok);
    assert(q.try_receive_from_isr(value)==Result::ok&&value==4);model::isr=false;
    AtomicU32 word(UINT32_MAX);assert(word.fetch_add(1)==UINT32_MAX&&word.load()==0);
    word.store(3);uint32_t expected=2;assert(!word.compare_exchange(expected,7)&&expected==3);
    assert(word.compare_exchange(expected,7)&&word.exchange(8)==7);
    assert(word.fetch_or(1)==8&&word.fetch_and(7)==9&&word.load()==1);
    model::isr=true;assert(word.fetch_add(1)==1);model::isr=false;assert(model::depth==0);
}
