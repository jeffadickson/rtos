#include "sync_model.hpp"
#include "rtos_uart.hpp"
#include <string.h>
using namespace rtos;
namespace {
struct SlowUart {
    char output[256]{};size_t used{};bool full=true;
    int write(const char* bytes,size_t count){
        assert(model::depth==0); // Device access never holds the IRQ critical lock.
        if(full)return 0;
        if(count>1)count=1; // Force every record to survive many partial writes.
        assert(used+count<sizeof(output));
        for(size_t i=0;i<count;i++)output[used++]=bytes[i];
        return int(count);
    }
};
}
int main(){
    Thread a,b,c;model::scheduler.add(a);model::scheduler.add(b);model::scheduler.add(c);
    SlowUart device;QueuedUart<2,64,SlowUart> uart(device);
    model::use(a);assert(uart.printf("hello from thread %u\n",1u).result==Result::ok);
    model::use(b);assert(uart.printf("hello from thread %u\n",2u).result==Result::ok);
    model::use(c);assert(uart.printf("hello from thread %u\n",3u).result==Result::full);
    assert(uart.service(1)==Result::busy&&uart.snapshot().pending);
    assert(uart.printf("hello from thread %u\n",3u).result==Result::ok);
    device.full=false;
    for(unsigned i=0;i<100&&(uart.snapshot().pending||uart.snapshot().queue.depth);i++){
        model::use(i%2?a:b); // Producers run between individual transmitted bytes.
        assert(uart.service(1)==Result::busy||!uart.snapshot().pending);
    }
    assert(strcmp(device.output,"hello from thread 1\nhello from thread 2\nhello from thread 3\n")==0);
    const auto info=uart.snapshot();assert(info.sent==3&&info.accepted==3&&info.dropped==1&&!info.pending&&!info.queue.depth);
    model::isr=true;assert(uart.printf("ISR").result==Result::context);model::isr=false;
    // Exercise the actual MMIO register adapter's full/no-spin and TX offsets.
    alignas(4) uint32_t registers[3]={0,0,8};AxiUartTx mmio(reinterpret_cast<uintptr_t>(registers));
    assert(mmio.write("A",1)==0&&registers[1]==0);registers[2]=4;
    assert(mmio.write("B",1)==1&&registers[1]=='B'&&registers[0]==0);
    assert(model::depth==0);
}
