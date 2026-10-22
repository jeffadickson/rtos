#include "common.hpp"
using namespace rtos;
static EventFlags* flags;static Thread consumer;static uint32_t consumed;
static void signal(){model::isr=true;assert(flags->set_from_isr(1)==Result::ok);model::isr=false;}
static void second_waiter(){model::use(consumer,9);model::on_park=signal;assert(flags->wait_any(1,consumed,3,EventClear::yes)==Result::ok);}
CASE(131,"NXR-021","wait-any observes all currently matched bits"){EventFlags e;e.set(6);uint32_t out=99;assert(e.wait_any(7,out)==Result::ok&&out==6&&e.snapshot().bits==6);}
CASE(132,"NXR-021","wait-all partial availability does not consume"){EventFlags e;e.set(1);uint32_t out=99;assert(e.wait_all(3,out,0,EventClear::yes)==Result::empty&&out==99&&e.snapshot().bits==1);}
CASE(133,"NXR-021","consuming wait preserves bits outside mask"){EventFlags e;e.set(15);uint32_t out;assert(e.wait_any(3,out,0,EventClear::yes)==Result::ok&&out==3&&e.snapshot().bits==12);}
CASE(134,"NXR-021","clear removes only requested stored bits"){EventFlags e;e.set(15);assert(e.clear(5)==Result::ok&&e.snapshot().bits==10);}
CASE(135,"NXR-021","zero set and clear preserve event storage"){EventFlags e;e.set(9);assert(e.set(0)==Result::invalid&&e.clear(0)==Result::invalid&&e.snapshot().bits==9);}
CASE(136,"NXR-021","zero wait mask preserves observed value"){EventFlags e;uint32_t out=99;assert(e.wait_any(0,out)==Result::invalid&&out==99);}
CASE(137,"NXR-021","invalid timeout does not consume available events"){EventFlags e;e.set(1);uint32_t out=99;assert(e.wait_any(1,out,MaxWait+1,EventClear::yes)==Result::invalid&&out==99&&e.snapshot().bits==1);}
CASE(138,"NXR-021","thread event operations reject ISR context"){EventFlags e;e.set(1);uint32_t out=99;model::isr=true;assert(e.set(2)==Result::context&&e.clear(1)==Result::context&&e.wait_any(1,out)==Result::context&&out==99);model::isr=false;assert(e.snapshot().bits==1);}
CASE(139,"NXR-021","repeated event sets coalesce instead of counting"){EventFlags e;e.set(1);e.set(1);uint32_t out;e.wait_any(1,out,0,EventClear::yes);assert(e.wait_any(1,out)==Result::empty);}
CASE(140,"NXR-126","broadcast waiter survives earlier consuming waiter"){EventFlags e;flags=&e;consumed=0;model::scheduler.add(consumer);model::on_park=second_waiter;uint32_t out=99;assert(e.wait_any(1,out,5)==Result::ok&&out==1&&consumed==1&&!e.snapshot().bits);flags=nullptr;model::on_park=nullptr;}
