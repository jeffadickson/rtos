/** @file Actual NexRT/lwIP/virtio guest. QEMU user networking forwards a loopback
 * host port to 10.0.2.15:7; no host socket implements the echo inside this guest.
 * The pinned single-hart virt map is probed only at its eight virtio slots.
 */
#include "rtos.hpp"
#include "rtos_udp.hpp"
#include "rtos_virtio_net.hpp"
#include "rtos_monitor_stream.hpp"
#if RTOS_SYNC
#include "rtos_mutex.hpp"
#endif
extern "C" {
struct QemuNetworkStatus {uint32_t phase,failure,echoes,peer_runs,received,transmitted;};
volatile QemuNetworkStatus nexrt_qemu_network_status{};
}
namespace {
constinit rtos::net::VirtioNet device;
constinit rtos::net::UdpEcho endpoint;
#if RTOS_TASK_TRACE
struct MonitorUart {size_t write(const uint8_t* bytes,size_t count){auto* uart=reinterpret_cast<volatile uint8_t*>(0x10000000);size_t sent=0;while(sent<count&&(uart[5]&32)){uart[0]=bytes[sent];++sent;}return sent;}};
constinit MonitorUart monitor_uart;
constinit rtos::monitor::Stream<MonitorUart> monitor_stream;
#endif
constinit rtos::Thread network_thread,peer_thread;
rtos::Stack<8192> network_stack;rtos::Stack<1024> peer_stack;
#if RTOS_SYNC && RTOS_TASK_TRACE
// A deterministic inversion/priority-inheritance witness. The low owner wakes
// the high waiter, inherits priority4, and then wakes medium priority2. Medium
// cannot steal the CPU from that donated owner before the mutex is released.
constinit rtos::Mutex demonstration_mutex;
constinit rtos::Semaphore owner_ready(0,1),medium_go(0,1);
constinit rtos::Thread owner_thread,waiter_thread,medium_thread;
rtos::Stack<2048> owner_stack,waiter_stack,medium_stack;
void owner(void*){if(demonstration_mutex.lock(100)!=rtos::Result::ok)rtos::exit();owner_ready.release();medium_go.release();
    const auto begin=rtos::ticks();while(rtos::Tick(rtos::ticks()-begin)<5){}demonstration_mutex.unlock();rtos::exit();}
void waiter(void*){owner_ready.acquire(100);if(demonstration_mutex.lock(100)==rtos::Result::ok)demonstration_mutex.unlock();rtos::exit();}
void medium(void*){medium_go.acquire(100);const auto begin=rtos::ticks();while(rtos::Tick(rtos::ticks()-begin)<3){}rtos::exit();}
#endif
// NS16550 polling is only used for finite readiness/failure messages, never in
// a trap or while the kernel masks IRQs. A missing UART cannot spin forever.
void console(const char* text){
    auto* uart=reinterpret_cast<volatile uint8_t*>(0x10000000);
    for(unsigned i=0;text[i]&&i<128;++i){unsigned tries=0;while(!(uart[5]&32)&&++tries<10000){}if(tries==10000)return;uart[0]=uint8_t(text[i]);}
}
void peer(void*){for(;;){nexrt_qemu_network_status.peer_runs=nexrt_qemu_network_status.peer_runs+1;rtos::sleep_for(1);}}
void network(void*){
    uintptr_t base=0;
    for(unsigned i=0;i<8;++i){const uintptr_t candidate=0x10001000u+i*0x1000u;
        if(*reinterpret_cast<volatile uint32_t*>(candidate)==0x74726976&&*reinterpret_cast<volatile uint32_t*>(candidate+8)==1){base=candidate;break;}}
    rtos::net::Ipv4Configuration address;address.address={10,0,2,15};address.gateway={10,0,2,2};
    if(!base||!device.initialize_mmio(base)||!endpoint.initialize(device.mac(),7,address)){
        nexrt_qemu_network_status.failure=1;console("NEXRT_QEMU_NET_FAILED\n");rtos::exit();}
    nexrt_qemu_network_status.phase=1;console("NEXRT_QEMU_NET_READY 10.0.2.15:7\n");
    for(;;){endpoint.poll(rtos::ticks(),4);const auto info=device.statistics();
        nexrt_qemu_network_status.echoes=endpoint.echoes();nexrt_qemu_network_status.received=info.received;nexrt_qemu_network_status.transmitted=info.transmitted;
#if RTOS_TASK_TRACE
        monitor_stream.service(monitor_uart);
#endif
        rtos::sleep_for(1);}
}
}
int main(){
    if(!rtos::create(network_thread,network,nullptr,5,network_stack,"network")||!rtos::create(peer_thread,peer,nullptr,0,peer_stack,"peer"))return 1;
#if RTOS_SYNC && RTOS_TASK_TRACE
    if(!rtos::create(owner_thread,owner,nullptr,1,owner_stack,"low-owner")||!rtos::create(waiter_thread,waiter,nullptr,4,waiter_stack,"high-waiter")||!rtos::create(medium_thread,medium,nullptr,2,medium_stack,"medium"))return 1;
#endif
    rtos::start(10000000,1000); // QEMU virt CLINT timebase, not the host CPU clock.
}
