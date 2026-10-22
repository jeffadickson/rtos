#include "common.hpp"
using namespace rtos;
static void ready(Thread& t,uint8_t p){t.state=State::ready;t.priority=t.effective_priority=p;}
CASE(151,"NXR-004","strict priority admits priority 255"){detail::Scheduler s;Thread a,b;ready(a,0);ready(b,255);s.add(a);s.add(b);assert(s.pick()==&b);}
CASE(152,"NXR-004","blocked highest priority never dispatches"){detail::Scheduler s;Thread a,b;ready(a,1);ready(b,255);b.state=State::blocked;s.add(a);s.add(b);assert(s.pick()==&a);}
CASE(153,"NXR-005","three equal-priority peers rotate FIFO"){detail::Scheduler s;Thread a,b,c;ready(a,7);ready(b,7);ready(c,7);s.add(a);s.add(b);s.add(c);assert(s.pick()==&a&&s.pick()==&b&&s.pick()==&c&&s.pick()==&a);}
CASE(154,"NXR-005","no-rotate keeps previous equal-priority thread"){detail::Scheduler s;Thread a,b;ready(a,7);ready(b,7);s.add(a);s.add(b);assert(s.pick(&b,false)==&b);}
CASE(155,"NXR-004","priority donation controls dispatch"){detail::Scheduler s;Thread a,b;ready(a,1);ready(b,3);a.effective_priority=9;s.add(a);s.add(b);assert(s.pick()==&a);}
CASE(156,"NXR-009","wake at exact deadline becomes ready"){detail::Scheduler s;Thread a;a.state=State::sleeping;a.wake=5;s.add(a);s.advance(4);assert(a.state==State::sleeping);s.advance(1);assert(a.state==State::ready);}
CASE(157,"NXR-009","sleep wrap deadline waits through zero"){detail::Scheduler s;Thread a;a.state=State::sleeping;a.wake=1;s.add(a);s.now=UINT32_MAX;s.advance(1);assert(a.state==State::sleeping);s.advance(1);assert(a.state==State::ready);}
CASE(158,"NXR-010","timer catch-up counts exact deadline once"){assert(detail::elapsed_ticks(100,100,17)==1&&detail::elapsed_ticks(116,100,17)==1&&detail::elapsed_ticks(117,100,17)==2);}
CASE(159,"NXR-003","adjacent storage ranges do not overlap"){assert(!detail::ranges_overlap(100,16,116,16)&&detail::ranges_overlap(100,17,116,16));}
CASE(160,"NXR-003","range overlap handles top address without addition overflow"){assert(detail::ranges_overlap(UINTPTR_MAX-7,8,UINTPTR_MAX-1,1)&&!detail::ranges_overlap(UINTPTR_MAX-7,4,UINTPTR_MAX-1,1));}
