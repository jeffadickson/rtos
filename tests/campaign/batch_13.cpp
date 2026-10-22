#include "common.hpp"
using namespace rtos;
static Semaphore* waiting;
static void give_from_irq(){model::isr=true;assert(waiting->release_from_isr()==Result::ok);model::isr=false;}
static void expire(){model::scheduler.advance(2);}
CASE(121,"NXR-019","zero semaphore maximum is invalid"){Semaphore s(0,0);assert(s.acquire()==Result::invalid&&s.release()==Result::invalid);}
CASE(122,"NXR-019","initial tokens above maximum stay invalid"){Semaphore s(3,2);assert(s.acquire()==Result::invalid&&s.snapshot().count==3);}
CASE(123,"NXR-019","UINT32_MAX maximum supports last token"){Semaphore s(UINT32_MAX-1,UINT32_MAX);assert(s.release()==Result::ok&&s.snapshot().count==UINT32_MAX&&s.release()==Result::overflow);}
CASE(124,"NXR-019","binary semaphore overflow loses no token"){Semaphore s(1,1);assert(s.release()==Result::overflow&&s.acquire()==Result::ok&&s.acquire()==Result::empty);}
CASE(125,"NXR-019","invalid wait does not consume available token"){Semaphore s(1,2);assert(s.acquire(MaxWait+1)==Result::invalid&&s.snapshot().count==1);}
CASE(126,"NXR-019","ISR-only release rejects ordinary thread"){Semaphore s(0,1);assert(s.release_from_isr()==Result::context&&!s.snapshot().count);}
CASE(127,"NXR-019","ISR release rejects stopped scheduler"){Semaphore s(0,1);model::started=false;model::isr=true;assert(s.release_from_isr()==Result::context&&!s.snapshot().count);model::started=true;model::isr=false;}
CASE(128,"NXR-019","ISR handoff wakes waiter without accumulating count"){Semaphore s(0,1);waiting=&s;model::on_park=give_from_irq;assert(s.acquire(3)==Result::ok&&!s.snapshot().count&&!s.snapshot().waiters);waiting=nullptr;model::on_park=nullptr;}
CASE(129,"NXR-019","release after timeout retains a usable token"){Semaphore s(0,1);model::on_park=expire;assert(s.acquire(2)==Result::timeout&&s.release()==Result::ok&&s.acquire()==Result::ok);}
CASE(130,"NXR-019","thread acquire from ISR leaves count unchanged"){Semaphore s(1,1);model::isr=true;assert(s.acquire()==Result::context&&s.snapshot().count==1);model::isr=false;}
