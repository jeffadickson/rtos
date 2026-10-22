/** @file Optional real-firmware link/execution probe for new diagnostic helpers. */
#include "rtos.hpp"
#include "rtos_trace.hpp"
#include "rtos_fault_record.hpp"
#include "rtos_periodic.hpp"
#include "rtos_mmio.hpp"
#include "rtos_uart.hpp"
namespace {
rtos::Thread worker;rtos::Stack<1024> stack;
rtos::TraceRing<2> trace;rtos::FirstFault fault;
rtos::AxiUartTx uart(0x10000000);rtos::DeviceWordIo bus;
rtos::Register32<rtos::RegisterAccess::read_only> status(bus,0x10000008);
void run(void*){
    rtos::TraceEvent event;rtos::FaultRecord first;
    rtos::PeriodicRelease periodic{rtos::ticks()+10,10};uint32_t delay=0,device=0;
    const bool ok=trace.push({rtos::ticks(),1,2,3})&&trace.pop(event)&&event.code==1&&
        fault.capture({7,8,9})&&!fault.capture({1})&&fault.snapshot(first)&&first.cause==7&&
        periodic.advance(rtos::ticks(),delay)&&delay>0&&delay<=10&&status.read(device)&&device==4;
    const char* text=ok?"campaign helpers PASS\n":"campaign helpers FAIL\n";
    while(*text){if(uart.write(text,1)==1)++text;else rtos::yield();}
    for(;;)rtos::sleep_for(10);
}
}
int main(){if(!rtos::create(worker,run,nullptr,2,stack,"campaign-probe"))return 1;rtos::start(100000000,1000);}
