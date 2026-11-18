/** @file
 * @brief Buildable DMA-channel example using a SOFTWARE adapter, not physical DMA.
 * Replace this adapter with the selected engine's reviewed BSP callbacks.
 * Debugger-visible counters let a board harness observe forward progress.
 */
#include "rtos_dma.hpp"
volatile uint32_t dma_example_passes{},dma_example_failures{};
namespace {
using namespace rtos::dma;
alignas(64) uint8_t storage[64];
Channel channel;
rtos::Thread worker;
rtos::Stack<1024> stack;
struct SoftwareEngine { Ticket ticket{};size_t length{}; };
SoftwareEngine engine;
bool accepts(void*,Direction direction,uint64_t address,size_t length){return direction==Direction::transmit&&address==0x1000&&length<=sizeof(storage);}
void prepare(void*,Direction,Buffer,size_t){} // Software adapter only; real BSP supplies cache/barrier work.
bool start(void* context,Direction,uint64_t,size_t length,Ticket ticket){
    auto& state=*static_cast<SoftwareEngine*>(context);state.ticket=ticket;state.length=length;return true;
}
Completion poll(void* context){
    auto& state=*static_cast<SoftwareEngine*>(context);
    return {state.ticket,storage[0]==1?Progress::completed:Progress::fault,state.length};
}
bool stop(void*){return true;} // No bus master exists in this software example.
void finish(void*,Direction,Buffer,size_t){}
void run(void*){
    for(;;){
        uint8_t command[]{1,2,3,4};Ticket ticket{};
        auto result=channel.transmit(command,sizeof(command),ticket);
        if(result==Status::ok){
            command[0]=99; // Adapter must see its permanent copy, not this caller buffer.
            const auto deadline=rtos::ticks()+10;
            while((result=channel.poll(ticket))==Status::pending){
                if(uint32_t(rtos::ticks()-deadline)<0x80000000u)break;
                (void)rtos::sleep_for(1);
            }
            size_t bytes=0;
            if(result==Status::ok&&channel.collect(ticket,nullptr,0,bytes)==Status::ok&&bytes==sizeof(command))dma_example_passes=dma_example_passes+1;
            else{dma_example_failures=dma_example_failures+1;(void)channel.cancel(ticket);}
        }else dma_example_failures=dma_example_failures+1;
        (void)rtos::sleep_for(1);
    }
}
}
int main(){
    if(!channel.initialize({&engine,accepts,prepare,start,poll,stop,finish},
        {storage,0x1000,sizeof(storage),64},Direction::transmit))return 1;
    if(!rtos::create(worker,run,nullptr,1,stack,"dma-example"))return 2;
#if defined(__arm__)
    rtos::start(25000000); // QEMU MPS2 only; replace for a physical board.
#else
    rtos::start(10000000); // Example RISCue timer rate; replace with the actual FPGA design.
#endif
}
