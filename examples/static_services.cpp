/** @file Actual NexRT worker demonstration, not a host-only library test.
 * Uses simulated NOR and an in-memory Ethernet wire endpoint. One worker owns
 * both services; a second worker supplies scheduling/preemption evidence. This
 * larger-RAM simulation is not evidence that these services fit a 32 KiB board.
 */
#include "rtos.hpp"
#include "rtos_littlefs.hpp"
#include "static_service_probe.hpp"
extern "C" {
struct ServiceStatus {uint32_t phase,failure,files,echoes,peer_runs,dispatches;};
volatile ServiceStatus nexrt_services_status{};
extern const volatile uint32_t nexrt_services_clock_hz=100000000;
}
namespace {
constinit rtos::Thread service_thread,peer_thread;
alignas(16) uint8_t service_stack[16384]{},peer_stack[2048]{};
constinit rtos::storage::SimFlash<32,512> flash;
constinit rtos::storage::Volume volume;
constinit nexrt::probe::Wire wire;
constinit rtos::net::UdpEcho network;
void fail(uint32_t code){nexrt_services_status.failure=code;nexrt_services_status.phase=3;for(;;)rtos::yield();}
void peer(void*){for(;;){nexrt_services_status.peer_runs=nexrt_services_status.peer_runs+1;rtos::sleep_for(1);}}
void service(void*){
    nexrt_services_status.phase=1;
    flash.erase_all();
    if(!volume.bind(flash.device())||!volume.format()||!volume.mount())fail(1);
    const char value[]="NexRT littlefs survives remount";
    if(!volume.write("state",value,sizeof(value))||!volume.unmount()||!volume.mount())fail(2);
    char result[sizeof(value)]{};
    if(volume.read("state",result,sizeof(result))!=sizeof(result)||memcmp(value,result,sizeof(value)))fail(3);
    nexrt_services_status.files=1;
    if(!network.initialize(wire.mac()))fail(4);
    wire.arp();network.poll(0);
    uint8_t payload[64]{};
    for(unsigned job=0;job<32;++job){
        for(unsigned i=0;i<sizeof(payload);++i)payload[i]=uint8_t(job*37+i);
        wire.udp(payload,sizeof(payload));network.poll(rtos::ticks());
        if(!wire.echoed(payload,sizeof(payload)))fail(5);
        nexrt_services_status.echoes=network.echoes();rtos::sleep_for(1);
    }
    nexrt_services_status.echoes=network.echoes();
    rtos::ThreadInfo threads[3]{};rtos::snapshot(threads,3);
    for(const auto& thread:threads)if(thread.id==&service_thread)nexrt_services_status.dispatches=uint32_t(thread.dispatches);
    if(!nexrt_services_status.peer_runs)fail(6);
    nexrt_services_status.phase=2;for(;;)rtos::sleep_for(100);
}
}
int main(){
    if(!rtos::create(service_thread,service,nullptr,2,service_stack,sizeof(service_stack),"services")||
       !rtos::create(peer_thread,peer,nullptr,1,peer_stack,sizeof(peer_stack),"peer"))return 1;
    rtos::start(nexrt_services_clock_hz,1000);
}
