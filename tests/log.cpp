#include "sync_model.hpp"
#include "rtos_log.hpp"
#include <string.h>
using namespace rtos;
static Logger<2,64> logger;
struct Output {char text[1024]{};size_t size{},max_call{};int behavior{};};
int writer(void* pointer,const char* text,size_t size){
    auto& out=*static_cast<Output*>(pointer);if(out.behavior==1)return 0;if(out.behavior==2)return -1;if(out.behavior==3)return int(size+1);
    if(size>out.max_call)out.max_call=size;
    if(size>3)size=3;
    assert(out.size+size<sizeof(out.text));for(size_t i=0;i<size;++i)out.text[out.size++]=text[i];return int(size);
}
void drain_all(Output& output){for(unsigned i=0;i<100&&(logger.snapshot().queue.depth||logger.snapshot().pending);++i)(void)logger.drain(writer,&output,5);}
int main(){
    Thread a,b;model::use(a);model::scheduler.add(a);model::scheduler.add(b);
    assert(logger.printf("A:%d %u %x %%",-12,42u,0xabu).result==Result::ok);
    model::use(b,2);assert(logger.printf("B:%s %c","ok",'!').result==Result::ok);
    assert(logger.printf("dropped").result==Result::full);
    Output output;output.behavior=1;assert(logger.drain(writer,&output,5)==Result::busy&&logger.snapshot().pending);
    output.behavior=0;drain_all(output);
    assert(strcmp(output.text,"A:-12 42 ab %B:ok !")==0&&output.max_call<=5);
    assert(logger.printf("%f",1.0).result==Result::invalid);
    assert(logger.printf("%lld %llu",(-9223372036854775807LL-1),18446744073709551615ULL).result==Result::ok);drain_all(output);
    assert(strstr(output.text,"-9223372036854775808 18446744073709551615"));
    char large[100];for(auto& c:large)c='z';assert(logger.write(large,sizeof(large)).truncated);drain_all(output);
    model::isr=true;assert(logger.printf("bad context").result==Result::context);
    assert(logger.write_from_isr("IRQ",3).result==Result::ok);model::isr=false;drain_all(output);
    assert(logger.write_from_isr("IRQ",3).result==Result::context);
    assert(logger.write("retry",5).result==Result::ok);output.behavior=2;assert(logger.drain(writer,&output)==Result::invalid&&logger.snapshot().pending);
    output.behavior=3;assert(logger.drain(writer,&output)==Result::invalid);output.behavior=0;drain_all(output);
    auto info=logger.snapshot();assert(info.accepted==6&&info.dropped==1&&info.truncated==1&&info.sent==6&&info.errors==3);
    assert(logger.write(nullptr,1).result==Result::invalid);assert(logger.drain(nullptr,&output)==Result::invalid);
    assert(model::depth==0);
}
