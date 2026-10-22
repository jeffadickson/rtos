#include "common.hpp"
#include "rtos_uart.hpp"
using namespace rtos;
struct Sink {char bytes[256]{};size_t used{};int limit=1;int write(const char* p,size_t n){assert(model::depth==0);if(limit<0)return limit;if(n>size_t(limit))n=size_t(limit);for(size_t i=0;i<n;i++)bytes[used++]=p[i];return int(n);}};
CASE(51,"NXR-121","queued UART copies reusable producer buffer"){Sink s;QueuedUart<2,8,Sink> q(s);char b[]="abc";q.write(b,3);b[0]='X';q.service(8);assert(s.used==3&&memcmp(s.bytes,"abc",3)==0);}
CASE(52,"NXR-121","queued UART full record is rejected whole"){Sink s;QueuedUart<1,8,Sink> q(s);q.write("a",1);assert(q.write("bc",2).result==Result::full);q.service();assert(s.used==1&&s.bytes[0]=='a');}
CASE(53,"NXR-121","blocked transport retains record offset"){Sink s;s.limit=0;QueuedUart<1,8,Sink> q(s);q.write("abc",3);assert(q.service()==Result::busy&&q.snapshot().pending&&q.snapshot().offset==0);}
CASE(54,"NXR-121","byte budget limits each service"){Sink s;QueuedUart<1,8,Sink> q(s);q.write("abc",3);q.service(1);assert(s.used==1&&q.snapshot().offset==1);}
CASE(55,"NXR-121","negative transport result preserves pending record"){Sink s;s.limit=-1;QueuedUart<1,8,Sink> q(s);q.write("abc",3);assert(q.service()==Result::invalid&&q.snapshot().pending&&!q.snapshot().offset);}
CASE(56,"NXR-121","transport recovery continues original record"){Sink s;s.limit=0;QueuedUart<1,8,Sink> q(s);q.write("abc",3);q.service();s.limit=1;q.service();assert(s.used==3&&q.snapshot().sent==1);}
CASE(57,"NXR-121","null producer input rejected without record"){Sink s;QueuedUart<1,8,Sink> q(s);assert(q.write(nullptr,1).result==Result::invalid&&!q.snapshot().accepted);}
CASE(58,"NXR-121","zero service budget leaves queue intact"){Sink s;QueuedUart<1,8,Sink> q(s);q.write("abc",3);assert(q.service(0)==Result::invalid&&q.snapshot().queue.depth==1&&!s.used);}
CASE(59,"NXR-121","truncated record is explicitly reported"){Sink s;QueuedUart<1,2,Sink> q(s);auto r=q.write("abc",3);assert(r.result==Result::ok&&r.truncated&&r.bytes==2&&q.snapshot().truncated==1);}
CASE(60,"NXR-121","queued UART rejects ISR service and formatting"){Sink s;QueuedUart<1,8,Sink> q(s);model::isr=true;assert(q.printf("x").result==Result::context&&q.service()==Result::context&&!s.used);model::isr=false;}
