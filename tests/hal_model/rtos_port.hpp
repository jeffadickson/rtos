#pragma once
/** @file Test-only processor provider; never selected by a firmware target. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
namespace rtos::port {
using InterruptState=uint32_t;
inline InterruptState mask=7;
inline uint64_t clock{},deadline{};
inline unsigned idles{},vectors{},timers{},schedules{};
inline void(*entry_seen)(){};
inline InterruptState mask_interrupts(){auto old=mask;mask=0;return old;}
inline void restore_interrupts(InterruptState saved){mask=saved;}
inline void idle(){++idles;}
[[noreturn]] inline void halt(){abort();}
inline uint64_t time_now(){return clock;}
inline void arm(uint64_t value){deadline=value;}
inline uint32_t* initialize_context(void* stack,size_t bytes,void(*entry)()){
    entry_seen=entry;return static_cast<uint32_t*>(stack)+bytes/sizeof(uint32_t)-4;
}
inline void install_vector(){++vectors;}
inline void enable_timer(){++timers;}
inline void reschedule(){++schedules;}
inline bool is_yield(uint32_t cause){return cause==91;}
inline bool is_tick(uint32_t cause){return cause==92;}
inline void resume_after_yield(uint32_t* frame){frame[0]+=8;}
}
