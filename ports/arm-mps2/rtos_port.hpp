#pragma once
/** @file
 * @brief QEMU MPS2 AN385 Cortex-M3 simulation port, privileged single CPU.
 * SVC remains callable inside BASEPRI critical sections; SysTick is masked.
 * No external IRQ dispatch, FPU, MPU, TLS or board qualification is supplied.
 */
#include <stddef.h>
#include <stdint.h>
#ifndef RTOS_ARM_TIMER_INITIAL
#define RTOS_ARM_TIMER_INITIAL UINT32_MAX
#endif
static_assert(RTOS_ARM_TIMER_INITIAL>0 && RTOS_ARM_TIMER_INITIAL<=UINT32_MAX);
extern "C" uint32_t arm_vectors[];
#if RTOS_ARM_STRESS_WITNESS
extern "C" void rtos_arm_stress_counter(uint64_t extended,uint32_t low);
#endif
namespace rtos::port {
using InterruptState=uint32_t;
/// Save BASEPRI and mask priority 0x80 and below, preserving a stricter mask.
inline InterruptState mask_interrupts(){
    uint32_t previous;
    asm volatile("mrs %0, basepri\nmsr basepri_max, %1\ndsb\nisb"
                 : "=&r"(previous) : "r"(0x80u) : "memory");
    return previous;
}
/// Restore the caller's mask exactly; SVC priority zero is never masked here.
inline void restore_interrupts(InterruptState previous){
    asm volatile("msr basepri, %0\ndsb\nisb" :: "r"(previous) : "memory");
}
inline void idle(){asm volatile("wfi" ::: "memory");}
/// Fail closed: disable all configurable exceptions via PRIMASK and remain halted.
[[noreturn]] inline void halt(){asm volatile("cpsid i\ndsb\nisb" ::: "memory");for(;;)idle();}
inline uint32_t last_counter{};
inline uint64_t counter_epoch{};
/// CMSDK timer zero is a free-running 25 MHz downcounter, without its IRQ.
/// Serialized callers extend wraps; masking for a full 32-bit period is unsupported.
inline uint64_t time_now(){
    const uint32_t low=UINT32_MAX-*reinterpret_cast<volatile uint32_t*>(0x40000004);
    if(low<last_counter)counter_epoch+=uint64_t(1)<<32;
    last_counter=low;
#if RTOS_ARM_STRESS_WITNESS
    rtos_arm_stress_counter(counter_epoch|low,low);
#endif
    return counter_epoch|low;
}
/// Rearm SysTick to the absolute deadline, including catch-up after late delivery.
inline void arm(uint64_t deadline){
    const auto now=time_now();
    const uint64_t delta=deadline>now?deadline-now:0;
    // A zero reload disables SysTick. Retrying a due/near deadline therefore
    // needs at least two cycles; the kernel accounts any lateness on delivery.
    const uint64_t remaining=delta>2?delta:2;
    if(remaining>0x1000000u)halt(); // SysTick has a 24-bit reload register.
    auto* systick=reinterpret_cast<volatile uint32_t*>(0xe000e010);
    systick[0]=0;systick[1]=uint32_t(remaining-1);systick[2]=0;systick[0]=7;
}
/// Frame: BASEPRI, EXC_RETURN, r4..r11, then hardware r0..r3/r12/lr/pc/xPSR.
inline uint32_t* initialize_context(void* stack,size_t bytes,void(*entry)()){
    auto* frame=reinterpret_cast<uint32_t*>(static_cast<char*>(stack)+bytes)-18;
    for(unsigned i=0;i<18;++i)frame[i]=0;
    frame[1]=0xfffffffdu; // Thread mode, PSP, basic integer frame.
    frame[16]=reinterpret_cast<uintptr_t>(entry)&~uintptr_t(1);
    frame[17]=0x01000000u; // Thumb state; initial PSP has 8-byte alignment.
    return frame;
}
/// This BSP owns the vector table, CMSDK timer zero and SysTick exclusively.
inline void install_vector(){
    // AAPCS handler calls require 8-byte MSP alignment, including interrupted
    // leaf assembly with a four-byte-aligned PSP. Hardware records padding in xPSR.
    *reinterpret_cast<volatile uint32_t*>(0xe000ed14)|=0x200;
    *reinterpret_cast<volatile uint32_t*>(0xe000ed08)=reinterpret_cast<uintptr_t>(arm_vectors);
    *reinterpret_cast<volatile uint8_t*>(0xe000ed1f)=0; // SVC priority zero.
    *reinterpret_cast<volatile uint8_t*>(0xe000ed23)=0x80; // SysTick priority.
#if RTOS_HW_PROTECT
    // Enable MemManage fault handler so MPU stack-guard violations trap synchronously.
    *reinterpret_cast<volatile uint32_t*>(0xe000ed24)|=(1u<<16);
#endif
    auto* timer=reinterpret_cast<volatile uint32_t*>(0x40000000);
    timer[0]=0;timer[2]=UINT32_MAX;timer[1]=RTOS_ARM_TIMER_INITIAL;timer[0]=1;
}
inline void enable_timer(){} // arm() enables SysTick; BASEPRI holds it until dispatch.
inline void enable_external_irq(){} // Board BSP enables specific NVIC lines when wired.
/// Optional ARMv7-M MPU Region 0 read-only 32-byte stack guard.
inline void configure_protection(uintptr_t stack_begin, size_t stack_bytes){
#if RTOS_HW_PROTECT
    auto* mpu=reinterpret_cast<volatile uint32_t*>(0xe000ed90);
    if(((mpu[0]>>8)&0xffu)==0)return; // CPU has no MPU regions.
    const uintptr_t guard_base=(stack_begin+31u)&~uintptr_t(31u);
    if(guard_base+32u>stack_begin+stack_bytes)return;
    mpu[1]=0; // Disable MPU while reprogramming Region 0.
    mpu[2]=0; // RNR = Region 0.
    mpu[3]=static_cast<uint32_t>(guard_base); // RBAR (32-byte aligned).
    // RASR: XN=1, AP=0b101 (Priv RO, Unpriv No Access), SIZE=4 (32 bytes), ENABLE=1.
    mpu[4]=(1u<<28)|(5u<<24)|(4u<<1)|1u;
    mpu[1]=5u; // ENABLE | PRIVDEFENA (keep background map for privileged kernel/app RAM).
    asm volatile("dsb\nisb" ::: "memory");
#else
    (void)stack_begin;(void)stack_bytes;
#endif
}
/// Synchronous SVC works with BASEPRI=0x80. Calling with PRIMASK set is unsupported.
inline void reschedule(){asm volatile("svc 0" ::: "memory");}
inline bool is_yield(uint32_t cause){return cause==11;}
inline bool is_tick(uint32_t cause){return cause==15;}
inline bool is_external_irq(uint32_t cause){return cause>=16;}
inline void resume_after_yield(uint32_t*){} // Hardware stacked PC already follows SVC.
}
