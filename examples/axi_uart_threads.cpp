/** @file
 * @brief Three permanent workers and a shared AXI UART transmit console.
 * The simulation wrapper supplies a 100 MHz timer and TX-only UART registers.
 * No board address or clock is inferred by this example.
 */
#include "rtos_ipc.hpp"
#include "rtos_uart.hpp"

namespace {
static rtos::Thread threads[3];
static rtos::Stack<1536> stacks[3];
static rtos::AxiUartTx uart(0x10000000);
static rtos::QueuedUart<8,64> console(uart);
static rtos::AtomicU32 started;

/** @brief Worker three owns draining; all workers may publish copied records. */
void drain_console() {
    for(;;) {
        const auto info=console.snapshot();
        if(!info.pending && !info.queue.depth) return;
        (void)console.service(64);
    }
}

/** @brief Retains independent arithmetic state across arbitrary preemption.
 * The first 30000 iterations contain no yield, sleep or IPC call. All three
 * workers must start before the first printed mask is sampled: this requires
 * timer preemption of CPU work, rather than a cooperative handoff.
 */
void work(void* argument) {
    const auto id=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
    started.fetch_or(1u<<(id-1));
    uint32_t value=0x12340000u+id;
    for(uint32_t round=1;;++round) {
        const uint32_t count=round==1?30000u:4096u;
        for(uint32_t i=0;i<count;++i) value=value*1664525u+1013904223u+id;
        const auto mask=started.load();
        for(;;) {
            const auto result=console.printf("hello from thread %u round=%u value=%u mask=%u\n",
                static_cast<unsigned>(id),static_cast<unsigned>(round),
                static_cast<unsigned>(value),static_cast<unsigned>(mask));
            if(result.result==rtos::Result::ok) break;
            if(id==3) drain_console();
            rtos::yield();
        }
        if(id==3) drain_console();
        rtos::sleep_for(10u+id*5u);
    }
}
}

/** @brief Registers exactly three workers plus kernel idle; all storage is static. */
int main() {
    for(uintptr_t i=0;i<3;++i)
        if(!rtos::create(threads[i],work,reinterpret_cast<void*>(i+1),2,
                         stacks[i],i==0?"worker-1":i==1?"worker-2":"worker-3")) return 1;
    rtos::start(100000000,1000);
}
