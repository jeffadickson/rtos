#include "common.hpp"
#include "dma_fixture.hpp"
using namespace campaign_dma;
static Ticket start(Fixture& f){assert(f.bind());Ticket t;const char data[4]={};assert(f.channel.transmit(data,4,t)==Status::ok);return t;}
CASE(91,"NXR-134","uncertain cancel quarantines without cache finish"){Fixture f;auto t=start(f);f.engine.stops=false;assert(f.channel.cancel(t)==Status::quarantined&&!f.engine.finishes&&f.engine.stops_called==1);}
CASE(92,"NXR-134","quarantine rejects new transfer without modifying ticket"){Fixture f;auto t=start(f);f.engine.stops=false;f.channel.cancel(t);Ticket next{99};assert(f.channel.transmit("a",1,next)==Status::quarantined&&next.generation==99&&f.engine.starts==1);}
CASE(93,"NXR-134","failed recovery retains hardware ownership"){Fixture f;auto t=start(f);f.engine.stops=false;f.channel.cancel(t);assert(f.channel.recover(t)==Status::quarantined&&!f.engine.finishes);}
CASE(94,"NXR-134","successful recovery finishes once then retires ticket"){Fixture f;auto t=start(f);f.engine.stops=false;f.channel.cancel(t);f.engine.stops=true;assert(f.channel.recover(t)==Status::ok&&f.engine.finishes==1&&f.channel.poll(t)==Status::stale);}
CASE(95,"NXR-134","quarantine collection preserves byte output"){Fixture f;auto t=start(f);f.engine.stops=false;f.channel.cancel(t);size_t n=88;assert(f.channel.collect(t,nullptr,0,n)==Status::quarantined&&n==88);}
CASE(96,"NXR-134","stale recovery ticket never calls stop"){Fixture f;auto t=start(f);assert(f.channel.recover({t.generation+1})==Status::stale&&!f.engine.stops_called);}
CASE(97,"NXR-134","short TX completion requires stop proof"){Fixture f;auto t=start(f);f.engine.result={t,Progress::completed,3};assert(f.channel.poll(t)==Status::device_error&&f.engine.stops_called==1&&f.engine.finishes==1);}
CASE(98,"NXR-134","oversized completion cannot release staging without stop"){Fixture f;auto t=start(f);f.engine.result={t,Progress::completed,5};f.engine.stops=false;assert(f.channel.poll(t)==Status::quarantined&&!f.engine.finishes);}
CASE(99,"NXR-134","invalid completion enum fails closed"){Fixture f;auto t=start(f);f.engine.result={t,static_cast<Progress>(99),4};assert(f.channel.poll(t)==Status::device_error&&f.engine.stops_called==1);}
CASE(100,"NXR-134","failed launch does not publish ticket"){Fixture f;f.bind();f.engine.launch=false;Ticket t{99};assert(f.channel.transmit("x",1,t)==Status::device_error&&t.generation==99&&f.engine.prepares==1&&f.engine.finishes==1);}
