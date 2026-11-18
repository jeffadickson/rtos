#include "rtos.hpp"
// External linkage preserves these diagnostic results for debugger inspection.
rtos::ThreadInfo rtos_thread_info[RTOS_MAX_THREADS + 1];
rtos::SystemInfo rtos_system_info;
namespace {
rtos::Thread fast, slow;
rtos::Stack<1024> fast_stack, slow_stack;
volatile uint32_t fast_count, slow_count;
void fast_task(void*) { for (;;) { fast_count=fast_count+1; rtos::sleep_for(10); } }
void slow_task(void*) {
    for (;;) {
        slow_count=slow_count+1;
        rtos_system_info=rtos::snapshot(rtos_thread_info,RTOS_MAX_THREADS+1);
        rtos::sleep_for(100);
    }
}
}
extern "C" int main() {
    if (!rtos::create(fast, fast_task, nullptr, 2, fast_stack, "fast") ||
        !rtos::create(slow, slow_task, nullptr, 1, slow_stack, "slow")) return 1;
    rtos::start(10000000);
}
