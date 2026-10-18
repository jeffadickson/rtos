#pragma once
#include <stddef.h>
#include <stdint.h>
extern "C" void rtos_vector();
namespace rtos::port {
using InterruptState = uint32_t;
inline InterruptState mask_interrupts() { InterruptState previous; asm volatile("csrrci %0, mstatus, 8" : "=r"(previous) :: "memory"); return previous; }
inline void restore_interrupts(InterruptState previous) { if(previous & 8) asm volatile("csrsi mstatus, 8" ::: "memory"); }
inline void idle() { asm volatile("wfi"); }
[[noreturn]] inline void halt() { (void)mask_interrupts(); for(;;) idle(); }
inline uint64_t time_now() {
    auto* timer=reinterpret_cast<volatile uint32_t*>(RTOS_MTIME_ADDRESS);
    uint32_t hi,lo,again;
    do { hi=timer[1]; lo=timer[0]; again=timer[1]; } while(hi!=again);
    return (uint64_t(hi)<<32)|lo;
}
inline void arm(uint64_t deadline) {
    auto* compare=reinterpret_cast<volatile uint32_t*>(RTOS_MTIMECMP_ADDRESS);
    compare[1]=0xffffffffu; compare[0]=uint32_t(deadline); compare[1]=deadline>>32;
}
inline uint32_t* initialize_context(void* stack,size_t bytes,void (*entry)()) {
    auto* frame=reinterpret_cast<uint32_t*>(static_cast<char*>(stack)+bytes)-36;
    for(unsigned i=0;i<36;++i) frame[i]=0;
    frame[2]=reinterpret_cast<uintptr_t>(frame+36);
    asm volatile("mv %0, gp" : "=r"(frame[3]));
    asm volatile("mv %0, tp" : "=r"(frame[4]));
    frame[32]=reinterpret_cast<uintptr_t>(entry); frame[33]=0x1880u;
    return frame;
}
inline void install_vector() { asm volatile("csrw mtvec, %0" :: "r"(&rtos_vector) : "memory"); }
inline void enable_timer() { asm volatile("csrw mie, %0" :: "r"(0x80u) : "memory"); }
inline void reschedule() { asm volatile("ecall" ::: "memory"); }
inline bool is_yield(uint32_t cause) { return cause==11; }
inline bool is_tick(uint32_t cause) { return cause==0x80000007u; }
inline void resume_after_yield(uint32_t* frame) { frame[32]+=4; }
}
