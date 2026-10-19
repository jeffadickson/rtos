#include "sync_model.hpp"
using namespace rtos;
static Semaphore tokens(0,2);
static EventFlags flags;
static Thread a,b;
void give(){model::use(b,2);assert(tokens.release()==Result::ok);}
void timeout(){model::scheduler.advance(3);}
void signal(){model::use(b,2);assert(flags.set(6)==Result::ok);}
int main(){
    model::use(a);assert(model::scheduler.add(a));assert(model::scheduler.add(b));
    assert(tokens.acquire()==Result::empty);assert(tokens.release()==Result::ok);
    assert(tokens.release()==Result::ok);assert(tokens.release()==Result::overflow);
    assert(tokens.acquire()==Result::ok&&tokens.acquire()==Result::ok);
    model::on_park=give;assert(tokens.acquire(5)==Result::ok);assert(tokens.snapshot().count==0);
    model::on_park=timeout;assert(tokens.acquire(3)==Result::timeout);assert(!a.wait.queue);
    model::scheduler.now=0xfffffffeu;assert(tokens.acquire(3)==Result::timeout);
    uint32_t observed=99;assert(flags.wait(1,observed)==Result::empty&&observed==99);
    assert(flags.wait(0,observed)==Result::invalid);assert(flags.set(7)==Result::ok);
    assert(flags.wait(3,observed,0,true,true)==Result::ok&&observed==3&&flags.snapshot().bits==4);
    assert(flags.clear(4)==Result::ok);model::on_park=signal;
    assert(flags.wait(6,observed,5,true,true)==Result::ok&&observed==6);
    model::on_park=timeout;observed=99;assert(flags.wait(1,observed,3)==Result::timeout&&observed==99);
    model::isr=true;assert(tokens.acquire()==Result::context);assert(tokens.release_from_isr()==Result::ok);
    assert(flags.set_from_isr(1)==Result::ok);model::isr=false;
    assert(tokens.release_from_isr()==Result::context);model::started=false;
    assert(tokens.acquire()==Result::context);model::started=true;
    Semaphore invalid(2,1);assert(invalid.acquire()==Result::invalid);
    assert(model::depth==0);
}
