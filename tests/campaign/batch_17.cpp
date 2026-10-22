#include "common.hpp"
#include "rtos_log.hpp"
using namespace rtos;
struct Output {char bytes[256]{};size_t used{},calls{};int mode{};};
static int sink(void* p,const char* text,size_t n){auto& o=*static_cast<Output*>(p);o.calls++;if(o.mode==1)return 0;if(o.mode==2)return -1;if(o.mode==3)return int(n+1);if(n>2)n=2;for(size_t i=0;i<n;i++)o.bytes[o.used++]=text[i];return int(n);}
static void twice(Logger<2,16>& q,const char* f,...){va_list a;va_start(a,f);assert(q.vprintf(f,a).result==Result::ok);assert(q.vprintf(f,a).result==Result::ok);va_end(a);}
CASE(161,"NXR-132","pending record completes before later producer record"){Logger<2,16> q;Output o;q.write("abc",3);q.drain(sink,&o,1);q.write("XYZ",3);q.drain(sink,&o);assert(o.used==6&&memcmp(o.bytes,"abcXYZ",6)==0);}
CASE(162,"NXR-132","temporary backpressure preserves already sent prefix"){Logger<1,16> q;Output o;q.write("abc",3);q.drain(sink,&o,1);o.mode=1;assert(q.drain(sink,&o)==Result::busy&&q.snapshot().offset==1);o.mode=0;q.drain(sink,&o);assert(o.used==3&&memcmp(o.bytes,"abc",3)==0);}
CASE(163,"NXR-132","transport error after prefix retains offset"){Logger<1,16> q;Output o;q.write("abc",3);q.drain(sink,&o,1);o.mode=2;assert(q.drain(sink,&o)==Result::invalid&&q.snapshot().offset==1&&q.snapshot().errors==1);}
CASE(164,"NXR-132","oversized acceptance count never commits pending bytes"){Logger<1,16> q;Output o;o.mode=3;q.write("abc",3);assert(q.drain(sink,&o)==Result::invalid&&!q.snapshot().offset&&!q.snapshot().sent);}
CASE(165,"NXR-132","drain byte budget bounds callback count and output"){Logger<1,16> q;Output o;q.write("abc",3);q.drain(sink,&o,1);assert(o.used==1&&o.calls==1&&q.snapshot().pending);}
CASE(166,"NXR-132","empty-record service bounded to sixteen iterations"){Logger<32,1> q;Output o;for(unsigned i=0;i<32;i++)q.write(nullptr,0);assert(q.drain(sink,&o)==Result::busy&&q.snapshot().sent==16&&!o.calls);}
CASE(167,"NXR-132","logging before start cannot publish a record"){Logger<1,8> q;model::started=false;assert(q.write("a",1).result==Result::context&&!q.snapshot().accepted);model::started=true;}
CASE(168,"NXR-132","binary NUL survives queued transport"){Logger<1,8> q;Output o;const char b[3]={'a',0,'b'};q.write(b,3);q.drain(sink,&o);assert(o.used==3&&memcmp(o.bytes,b,3)==0);}
CASE(169,"NXR-140","vprintf preserves caller va-list for repeated use"){Logger<2,16> q;Output o;twice(q,"%u:%s",7u,"x");q.drain(sink,&o);assert(o.used==6&&memcmp(o.bytes,"7:x7:x",6)==0);}
CASE(170,"NXR-140","invalid format never consumes record capacity"){Logger<1,8> q;const char* f="%q";assert(q.printf(f,0u).result==Result::invalid&&q.printf("ok").result==Result::ok&&q.printf("full").result==Result::full);}
