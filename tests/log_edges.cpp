#include "sync_model.hpp"
#include "rtos_log.hpp"
#include <limits.h>
#include <string.h>
using namespace rtos;
// Invoke the formatter directly to exercise deliberately unsupported formats.
static detail::FormatResult format(char* output,size_t capacity,const char* text,...){
    va_list args;va_start(args,text);
    auto result=detail::format_log(output,capacity,text,args);va_end(args);return result;
}
static Logger<20,2> empty_records;
static Logger<2,8> interleaved;
static Thread producer,consumer;
struct Output {char bytes[32]{};size_t used{},calls{};};
static int empty_writer(void*,const char*,size_t){assert(false);return -1;}
static int writer(void* context,const char* bytes,size_t count){
    assert(model::depth==0);
    auto& out=*static_cast<Output*>(context);
    if(out.calls++==0){
        model::use(producer);
        assert(interleaved.write("XYZ",3).result==Result::ok);
        const auto info=interleaved.snapshot();
        assert(info.pending&&info.offset==0&&info.queue.depth==1&&info.accepted==2&&info.sent==0);
        assert(interleaved.drain(writer,context)==Result::busy);
        model::caller=&consumer;
    }
    if(count>2)count=2;
    assert(out.used+count<sizeof(out.bytes));
    for(size_t i=0;i<count;++i)out.bytes[out.used++]=bytes[i];
    return int(count);
}
int main(){
    char text[128]{};auto result=format(text,sizeof(text),"");
    assert(result.valid&&result.size==0&&!result.truncated);
    assert(!format(nullptr,1,"x").valid&&!format(text,0,"x").valid&&!format(text,1,nullptr).valid);
    char guarded[]{'L','?','?','?','?','R'};
    result=format(guarded+1,4,"abcd");
    assert(result.valid&&result.size==4&&!result.truncated&&guarded[0]=='L'&&guarded[5]=='R');
    result=format(guarded+1,3,"abcd");
    assert(result.valid&&result.size==3&&result.truncated&&guarded[4]=='d');
    result=format(text,sizeof(text),"%c:%X:%lu:%ld:%p",0,0xabcdefu,ULONG_MAX,LONG_MIN,static_cast<void*>(nullptr));
    assert(result.size<sizeof(text));text[result.size]=0;
    assert(result.valid&&text[0]==0&&strstr(text+1,":ABCDEF:")&&strstr(text+1,":0x0"));
    assert(strstr(text+1,sizeof(long)==8?":-9223372036854775808:":":-2147483648:"));
    int untouched=123;
    assert(!format(text,sizeof(text),"%n",&untouched).valid&&untouched==123);
    assert(!format(text,sizeof(text),"%02u",1u).valid);
    assert(!format(text,sizeof(text),"%f",1.0).valid);
    assert(!format(text,sizeof(text),"%lf",1.0).valid);
    assert(!format(text,sizeof(text),"%").valid&&!format(text,sizeof(text),"%lll").valid);
    assert(!format(text,1,"long prefix %n",&untouched).valid&&untouched==123);
    char string[98];for(auto& ch:string)ch='s';string[95]=0;
    result=format(text,sizeof(text),"%s",string);assert(result.valid&&result.size==95&&!result.truncated);
    string[95]='s';string[96]=0;
    result=format(text,sizeof(text),"%s",string);assert(result.valid&&result.size==96&&result.truncated);
    result=format(text,sizeof(text),"%s",static_cast<const char*>(nullptr));
    assert(result.valid&&result.size==6&&memcmp(text,"(null)",6)==0);
    char boundary[257];for(auto& ch:boundary)ch='a';boundary[255]=0;
    result=format(text,sizeof(text),boundary);assert(result.valid&&result.size==128&&result.truncated);
    boundary[255]='a';boundary[256]=0;assert(!format(text,sizeof(text),boundary).valid);
    model::use(consumer);model::scheduler.add(consumer);model::scheduler.add(producer);
    for(unsigned i=0;i<20;++i)assert(empty_records.write(nullptr,0).result==Result::ok);
    assert(empty_records.drain(empty_writer,nullptr)==Result::busy);
    auto info=empty_records.snapshot();assert(info.sent==16&&info.queue.depth==4&&!info.pending);
    assert(empty_records.drain(empty_writer,nullptr)==Result::ok);
    info=empty_records.snapshot();assert(info.sent==20&&info.accepted==20&&info.queue.depth==0);
    assert(interleaved.write("abc",3).result==Result::ok);
    Output output;assert(interleaved.drain(writer,&output,32)==Result::ok);
    assert(output.used==6&&memcmp(output.bytes,"abcXYZ",6)==0&&interleaved.snapshot().sent==2);
    const auto before=interleaved.snapshot();
    assert(interleaved.drain(writer,&output,0)==Result::invalid);
    assert(interleaved.snapshot().sent==before.sent&&model::depth==0);
}
