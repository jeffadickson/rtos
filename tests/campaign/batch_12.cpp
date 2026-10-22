#include "common.hpp"
#include "rtos_queue.hpp"
using namespace rtos;
static void expire(){model::scheduler.advance(3);}
CASE(111,"NXR-023","ISR API rejects thread-context send unchanged"){Queue<int,1> q;assert(q.try_send_from_isr(1)==Result::context&&!q.snapshot().depth);}
CASE(112,"NXR-023","ISR API rejects thread-context receive unchanged"){Queue<int,1> q;q.send(4);int v=99;assert(q.try_receive_from_isr(v)==Result::context&&v==99&&q.snapshot().depth==1);}
CASE(113,"NXR-023","ISR send rejected before scheduler start"){Queue<int,1> q;model::started=false;model::isr=true;assert(q.try_send_from_isr(1)==Result::context);model::started=true;model::isr=false;}
CASE(114,"NXR-023","ISR receive rejected before scheduler start"){Queue<int,1> q;int v=99;model::started=false;model::isr=true;assert(q.try_receive_from_isr(v)==Result::context&&v==99);model::started=true;model::isr=false;}
CASE(115,"NXR-023","ISR send full does not replace record"){Queue<int,1> q;q.send(3);model::isr=true;assert(q.try_send_from_isr(4)==Result::full);model::isr=false;int v;q.receive(v);assert(v==3);}
CASE(116,"NXR-023","ISR empty receive preserves output"){Queue<int,1> q;int v=99;model::isr=true;assert(q.try_receive_from_isr(v)==Result::empty&&v==99);model::isr=false;}
CASE(117,"NXR-020","receive timeout unlinks suspended destination"){Queue<int,1> q;model::on_park=expire;int v=99;assert(q.receive(v,3)==Result::timeout&&v==99&&!model::caller->wait.data&&!model::caller->wait.queue);}
CASE(118,"NXR-020","sender timeout never admits stale stack value"){Queue<int,1> q;q.send(1);model::on_park=expire;assert(q.send(2,3)==Result::timeout);int v;q.receive(v);assert(v==1&&q.receive(v)==Result::empty);}
CASE(119,"NXR-020","timeout expiration handles tick wrap"){Queue<int,1> q;model::scheduler.now=UINT32_MAX-1;model::on_park=expire;int v=99;assert(q.receive(v,3)==Result::timeout&&model::scheduler.now==1&&v==99);}
CASE(120,"NXR-130","timeout diagnostic differs from empty observation"){Queue<int,1> q;model::on_park=expire;int v;q.receive(v,3);auto s=q.snapshot();assert(s.empty==1&&s.timeouts==1&&!s.receive_waiters);}
