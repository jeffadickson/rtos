#include "common.hpp"
#include "dma_fixture.hpp"
#include <new>
using namespace campaign_dma;
CASE(171,"NXR-134","unbound TX call preserves caller ticket"){Fixture f;Ticket t{99};assert(f.channel.transmit("x",1,t)==Status::invalid&&t.generation==99&&!f.engine.starts);}
CASE(172,"NXR-134","RX entry rejects a TX channel"){Fixture f;f.bind();Ticket t{99};assert(f.channel.receive(1,t)==Status::invalid&&t.generation==99&&!f.engine.starts);}
CASE(173,"NXR-134","TX entry rejects an RX channel"){Fixture f;f.bind(Direction::receive);Ticket t{99};assert(f.channel.transmit("x",1,t)==Status::invalid&&t.generation==99&&!f.engine.starts);}
CASE(174,"NXR-134","zero ticket never reaches completion backend"){Fixture f;f.bind();assert(f.channel.poll({0})==Status::stale&&!f.engine.polls);}
CASE(175,"NXR-134","stale cancel cannot stop active transfer"){Fixture f;f.bind();Ticket t;f.channel.transmit("x",1,t);assert(f.channel.cancel({t.generation+1})==Status::stale&&!f.engine.stops_called);}
CASE(176,"NXR-134","TX source cannot overlap its output ticket"){Fixture f;f.bind();Ticket t{99};assert(f.channel.transmit(&t,sizeof(t),t)==Status::invalid&&t.generation==99&&!f.engine.prepares);}
CASE(177,"NXR-134","collect byte-count output cannot alias staging"){Fixture f;f.bind();Ticket t;f.channel.transmit("x",1,t);f.engine.result={t,Progress::completed,1};f.channel.poll(t);auto* n=new(f.storage) size_t(99);assert(f.channel.collect(t,nullptr,0,*n)==Status::invalid&&*n==99);}
CASE(178,"NXR-134","diagnostic snapshot cannot overwrite staging"){Fixture f;f.bind();auto* s=new(f.storage) Snapshot{};s->capacity=99;assert(f.channel.snapshot(*s)==Status::invalid&&s->capacity==99);}
CASE(179,"NXR-134","failed launch generation is never reused"){Fixture f;f.bind();f.engine.launch=false;Ticket t{99};f.channel.transmit("x",1,t);f.engine.launch=true;assert(f.channel.transmit("x",1,t)==Status::ok&&t.generation==2);}
CASE(180,"NXR-134","cancel completed transfer does not finish cache twice"){Fixture f;f.bind();Ticket t;f.channel.transmit("x",1,t);f.engine.result={t,Progress::completed,1};f.channel.poll(t);assert(f.channel.cancel(t)==Status::cancelled&&f.engine.finishes==1&&!f.engine.stops_called);}
