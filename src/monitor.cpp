#include "rtos_monitor.hpp"
#include "rtos_hal.hpp"
namespace rtos::monitor {
namespace {
constinit Buffer<RTOS_TASK_TRACE_CAPACITY> buffer;
// Nested local masks restore precisely; no formatting, callbacks or IO inside.
struct Guard {hal::InterruptState saved{hal::mask_interrupts()};~Guard(){hal::restore_interrupts(saved);}};
}
void publish(Event event){Guard guard;buffer.push(event);}
bool next(Event& event){Guard guard;const bool present=buffer.pop(event);if(present&&event.kind==Kind::loss)event.time=hal::time_now();return present;}
Info snapshot(){Guard guard;return buffer.info();}
}
