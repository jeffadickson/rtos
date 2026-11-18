#include "rtos.hpp"
// One thread writes this debugger-visible counter. Volatile is not synchronization.
volatile uint32_t nano_iterations;
namespace {
// Permanent boot-owned storage; the kernel borrows these objects forever.
rtos::Thread task;
rtos::Stack<512> stack; // Example size, not a proven worst-case stack bound.
// EXAMPLE ONLY: replace with the actual board mtime frequency before execution.
constexpr uint32_t machine_timer_hz = 10'000'000;
void work(void*) {
    for (;;) {
        nano_iterations=nano_iterations+1; // Explicit volatile read/write, not an atomic RMW.
        rtos::sleep_for(1); // Tick units; blocking permits private idle to execute.
    }
}
}
extern "C" int main() {
    // Higher numeric priority wins; nano reserves one application registry slot.
    if (!rtos::create(task, work, nullptr, 1, stack, "nano")) return 1;
    rtos::start(machine_timer_hz); // Non-returning timer setup and first dispatch.
}
