#include "common.hpp"
#include "rtos_trace.hpp"
using namespace rtos;
CASE(11,"NXR-072","empty trace preserves output"){TraceRing<2> q;TraceEvent e{7,8,9,10};assert(!q.pop(e)&&e.tick==7&&e.code==8);}
CASE(12,"NXR-072","binary event round trip"){TraceRing<2> q;TraceEvent e;assert(q.push({4,0xffffffff,0x80000000,0}));assert(q.pop(e)&&e.tick==4&&e.code==UINT32_MAX&&e.first==0x80000000&&!e.second);}
CASE(13,"NXR-072","trace FIFO order"){TraceRing<2> q;TraceEvent e;q.push({1});q.push({2});assert(q.pop(e)&&e.tick==1);assert(q.pop(e)&&e.tick==2);}
CASE(14,"NXR-072","full trace drops new event"){TraceRing<1> q;TraceEvent e;assert(q.push({1})&&!q.push({2}));assert(q.pop(e)&&e.tick==1);}
CASE(15,"NXR-072","drop is visible in snapshot"){TraceRing<1> q;q.push({});q.push({});q.push({});auto s=q.snapshot();assert(s.depth==1&&s.capacity==1&&s.dropped==2);}
CASE(16,"NXR-072","drained slots can wrap"){TraceRing<2> q;TraceEvent e;q.push({1});q.push({2});q.pop(e);q.push({3});q.pop(e);assert(e.tick==2);q.pop(e);assert(e.tick==3);}
CASE(17,"NXR-072","ISR trace restores nested mask"){TraceRing<1> q;model::isr=true;model::depth=2;assert(q.push({9})&&model::depth==2);model::depth=0;model::isr=false;}
CASE(18,"NXR-072","trace publication copies caller data"){TraceRing<1> q;TraceEvent a{5,6},b;q.push(a);a.code=99;q.pop(b);assert(b.code==6);}
CASE(19,"NXR-072","trace does not request scheduling"){TraceRing<1> q;auto n=model::reschedules;q.push({});TraceEvent e;q.pop(e);assert(model::reschedules==n);}
CASE(20,"NXR-072","drop history survives draining"){TraceRing<1> q;q.push({});q.push({});TraceEvent e;q.pop(e);assert(q.snapshot().depth==0&&q.snapshot().dropped==1);}
