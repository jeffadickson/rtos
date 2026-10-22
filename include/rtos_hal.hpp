#pragma once
/** @file
 * @brief Compile-time kernel HAL implemented by the selected processor/BSP port.
 * No virtual calls, runtime registration, storage or allocation is introduced.
 * The selected port supplies rtos_port.hpp on the kernel's private include path.
 * Applications use rtos.hpp; this interface is for kernel and port developers.
 */
#include "rtos_port.hpp"
namespace rtos::hal {
/// Opaque saved local scheduling mask. Preserve and restore the caller's value.
using InterruptState=port::InterruptState;
/// Mask local scheduling interrupts; synchronous scheduling must remain possible.
inline InterruptState mask_interrupts(){return port::mask_interrupts();}
/// Restore exactly the saved scheduling mask, including nested critical sections.
inline void restore_interrupts(InterruptState saved){port::restore_interrupts(saved);}
/// Processor idle hint; it may return immediately on some implementations.
inline void idle(){port::idle();}
/// Fail closed without returning to an unsafe kernel context.
[[noreturn]] inline void halt(){port::halt();}
/// Read monotonic timer units while the caller serializes extension metadata.
inline uint64_t time_now(){return port::time_now();}
/// Program the next absolute timer deadline in the same units as time_now().
inline void arm(uint64_t deadline){port::arm(deadline);}
/// Construct the processor's initial saved frame in permanent aligned storage.
inline uint32_t* initialize_context(void* stack,size_t bytes,void(*entry)()){
    return port::initialize_context(stack,bytes,entry);
}
/// Install the BSP's trap/vector and timer ownership before first dispatch.
inline void install_vector(){port::install_vector();}
/// Enable the port's scheduling timer source before first dispatch.
inline void enable_timer(){port::enable_timer();}
/// Enter the synchronous scheduling exception, also inside a local critical section.
inline void reschedule(){port::reschedule();}
/// Classify the port-supplied exception identity without architectural constants in the kernel.
inline bool is_yield(uint32_t cause){return port::is_yield(cause);}
/// Classify the scheduling timer exception.
inline bool is_tick(uint32_t cause){return port::is_tick(cause);}
/// Advance the saved PC only if the processor's synchronous exception requires it.
inline void resume_after_yield(uint32_t* frame){port::resume_after_yield(frame);}
}
