#include "common.hpp"
#include "rtos_log.hpp"
using namespace rtos;
static detail::FormatResult fmt(char* out,size_t n,const char* f,...){va_list a;va_start(a,f);auto r=detail::format_log(out,n,f,a);va_end(a);return r;}
CASE(61,"NXR-140","embedded NUL is part of length-delimited output"){char b[8]{};auto r=fmt(b,8,"a%cb",0);assert(r.valid&&r.size==3&&b[0]=='a'&&b[1]==0&&b[2]=='b');}
CASE(62,"NXR-140","string data is never reparsed as format"){char b[16]{};auto r=fmt(b,16,"%s","%n%u");assert(r.valid&&r.size==4&&memcmp(b,"%n%u",4)==0);}
CASE(63,"NXR-140","escaped percent followed by conversion"){char b[8]{};auto r=fmt(b,8,"%%%u",7u);assert(r.valid&&r.size==2&&memcmp(b,"%7",2)==0);}
CASE(64,"NXR-140","wide-character modifier rejected without argument read"){char b[8]{};assert(!fmt(b,8,"%lc").valid);}
CASE(65,"NXR-140","wide-string modifier rejected without pointer read"){char b[8]{};assert(!fmt(b,8,"%ls").valid);}
CASE(66,"NXR-140","length-qualified pointer rejected"){char b[8]{};assert(!fmt(b,8,"%lp").valid);}
CASE(67,"NXR-140","unsupported conversion after text publishes nothing"){Logger<1,16> q;const char* f="prefix %q";assert(q.printf(f,0u).result==Result::invalid&&q.snapshot().accepted==0&&q.snapshot().errors==1);}
CASE(68,"NXR-140","format exact capacity leaves trailing sentinel"){char b[5]={'x','x','x','x','Z'};auto r=fmt(b,4,"%u",1234u);assert(r.valid&&!r.truncated&&r.size==4&&b[4]=='Z');}
CASE(69,"NXR-130","diagnostic increment saturates rather than wrapping"){uint32_t value=UINT32_MAX-1;detail::increment(value);detail::increment(value);assert(value==UINT32_MAX);}
CASE(70,"NXR-140","invalid format remains invalid after output fills"){char b[1];assert(!fmt(b,1,"abcd%ls").valid);}
