#include "common.hpp"
#include "rtos_queue.hpp"
using namespace rtos;
static Queue<int,1>* active;static Thread helper;static int* original;
static void handoff(){model::use(helper,2);assert(active->send(77)==Result::ok);}
static void refill(){model::use(helper,2);*original=99;int v;assert(active->receive(v)==Result::ok&&v==11);}
CASE(101,"NXR-022","non-power-of-two FIFO wraps correctly"){Queue<int,3> q;int v;for(int i=0;i<3;i++)q.send(i);q.receive(v);q.send(3);for(int i=1;i<4;i++){assert(q.receive(v)==Result::ok&&v==i);}}
CASE(102,"NXR-022","queued object is a value copy"){struct V{int a,b;};Queue<V,1> q;V a{4,5},b{};q.send(a);a.a=9;q.receive(b);assert(b.a==4&&b.b==5);}
CASE(103,"NXR-022","capacity-sized payload copied exactly"){struct V{char data[RTOS_IPC_MAX_MESSAGE_BYTES];};Queue<V,1> q;V a{},b{};a.data[0]=1;a.data[sizeof(a.data)-1]=2;q.send(a);q.receive(b);assert(b.data[0]==1&&b.data[sizeof(b.data)-1]==2);}
CASE(104,"NXR-022","full admission preserves queued head"){Queue<int,1> q;int v;q.send(1);assert(q.send(2)==Result::full);q.receive(v);assert(v==1);}
CASE(105,"NXR-022","empty receive preserves caller bytes"){Queue<int,1> q;int v=99;assert(q.receive(v)==Result::empty&&v==99);}
CASE(106,"NXR-020","blocked receiver receives copied direct handoff"){Queue<int,1> q;active=&q;model::scheduler.add(helper);model::on_park=handoff;int v=99;assert(q.receive(v,5)==Result::ok&&v==77&&q.snapshot().depth==0);active=nullptr;model::on_park=nullptr;}
CASE(107,"NXR-020","blocked sender stages value before caller mutation"){Queue<int,1> q;active=&q;int v=22;original=&v;q.send(11);model::scheduler.add(helper);model::on_park=refill;assert(q.send(v,5)==Result::ok&&v==99);assert(q.receive(v)==Result::ok&&v==22);active=nullptr;original=nullptr;model::on_park=nullptr;}
CASE(108,"NXR-022","maximum finite timeout valid on immediate success"){Queue<int,1> q;assert(q.send(7,MaxWait)==Result::ok);int v;assert(q.receive(v,MaxWait)==Result::ok&&v==7);}
CASE(109,"NXR-022","oversized timeout cannot consume queued value"){Queue<int,1> q;q.send(7);int v=99;assert(q.receive(v,MaxWait+1)==Result::invalid&&v==99&&q.snapshot().depth==1);}
CASE(110,"NXR-130","queue high-water survives draining"){Queue<int,3> q;q.send(1);q.send(2);int v;q.receive(v);q.receive(v);auto s=q.snapshot();assert(s.high_water==2&&!s.depth&&s.capacity==3);}
