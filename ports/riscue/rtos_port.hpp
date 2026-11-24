#pragma once
#include <stddef.h>
#include <stdint.h>
extern "C" void rtos_vector();
extern "C" char _stack_top[];
namespace rtos::port {
using InterruptState = uint32_t;
/** Atomically read mstatus and clear MIE (bit 3) on this hart. The '=r' output
 * permits compiler register allocation; immediate 8 is the CSR bit mask. The
 * memory clobber orders compiler RAM accesses, not hardware MMIO, DMA or SMP.
 */
inline InterruptState mask_interrupts() { InterruptState previous; asm volatile("csrrci %0, mstatus, 8" : "=r"(previous) :: "memory"); return previous; }
/// Restore saved MIE only. Nested callers unwind in order with MIE still masked.
inline void restore_interrupts(InterruptState previous) { if(previous & 8) asm volatile("csrsi mstatus, 8" ::: "memory"); }
/// Power/wait hint; current RISCue may implement WFI as a NOP.
inline void idle() { asm volatile("wfi"); }
/// Fail closed: disable interrupts and remain in the hint loop forever.
[[noreturn]] inline void halt() { (void)mask_interrupts(); for(;;) idle(); }
/// Read split MMIO high/low/high; retry on rollover to avoid a torn RV32 value.
/// Board integration must provide accessible, ordered 32-bit timer registers.
inline uint64_t time_now() {
    auto* timer=reinterpret_cast<volatile uint32_t*>(RTOS_MTIME_ADDRESS);
    uint32_t hi,lo,again;
    do { hi=timer[1]; lo=timer[0]; again=timer[1]; } while(hi!=again);
    return (uint64_t(hi)<<32)|lo;
}
/// High=max, low, final high prevents a transient early compare on this board.
/// This assumes ordered MMIO; another port must supply its own verified ordering.
inline void arm(uint64_t deadline) {
    auto* compare=reinterpret_cast<volatile uint32_t*>(RTOS_MTIMECMP_ADDRESS);
    compare[1]=0xffffffffu; compare[0]=uint32_t(deadline); compare[1]=deadline>>32;
}
/// Maximum single tickless hardware sleep span in cycles (64-bit mtimecmp bounded to 32-bit span).
constexpr uint64_t max_tickless_cycles() { return 0x7fffffffULL; }
/** Construct context.S's 36-word frame in validated permanent stack storage.
 * Slot 2 is the original stack top, slot 32 the entry trampoline. 0x1880 means
 * MPP=M, MPIE=1, MIE=0: the first mret enables IRQs after restoring all registers.
 * gp/tp inherit bootstrap register values; tp preservation is not TLS support.
 * See the Sphinx assembly-port frame table before changing either implementation.
 */
inline uint32_t* initialize_context(void* stack,size_t bytes,void (*entry)()) {
    auto* frame=reinterpret_cast<uint32_t*>(static_cast<char*>(stack)+bytes)-36;
    for(unsigned i=0;i<36;++i) frame[i]=0;
    frame[2]=reinterpret_cast<uintptr_t>(frame+36);
    asm volatile("mv %0, gp" : "=r"(frame[3])); // Allocated register output is stored in x3's slot.
    asm volatile("mv %0, tp" : "=r"(frame[4])); // Preserve bootstrap x4 without inventing a TLS base.
    frame[32]=reinterpret_cast<uintptr_t>(entry); frame[33]=0x1880u;
    return frame;
}
/// Set the dedicated trap stack in mscratch and aligned direct mtvec.
inline void install_vector() {
    asm volatile("csrw mscratch, %0" :: "r"(_stack_top) : "memory");
    asm volatile("csrw mtvec, %0" :: "r"(&rtos_vector) : "memory");
}
/// Enable MTIE (bit 7); external interrupts remain masked unless explicitly enabled.
inline void enable_timer() { asm volatile("csrs mie, %0" :: "r"(0x80u) : "memory"); }
/// Enable MEIE (bit 11) for a registered external peripheral IRQ handler.
inline void enable_external_irq() { asm volatile("csrs mie, %0" :: "r"(0x800u) : "memory"); }
/// Optional PMP stack-guard region for PMP-equipped RISC-V cores.
inline void configure_protection(uintptr_t stack_begin, size_t stack_bytes) {
#if RTOS_HW_PROTECT
    const uintptr_t guard_base = (stack_begin + 31u) & ~uintptr_t(31u);
    if (guard_base + 32u > stack_begin + stack_bytes) return;
    // Region 0: 32-byte NAPOT read-only guard (R=1, W=0, X=0, A=NAPOT=3 -> 0x19).
    // Region 1: full 4 GiB NAPOT RWX background (R=1, W=1, X=1, A=NAPOT=3 -> 0x1F).
    const uint32_t pmp0 = static_cast<uint32_t>(((guard_base >> 2) & ~uintptr_t(7u)) | 3u);
    const uint32_t pmp1 = 0xFFFFFFFFu;
    const uint32_t cfg0 = 0x00001F19u;
    asm volatile("csrw pmpaddr0, %0\ncsrw pmpaddr1, %1\ncsrw pmpcfg0, %2"
                 :: "r"(pmp0), "r"(pmp1), "r"(cfg0) : "memory");
#else
    (void)stack_begin; (void)stack_bytes;
#endif
}
/// A synchronous 32-bit ECALL traps even with MIE clear; memory clobber orders RAM.
inline void reschedule() { asm volatile("ecall" ::: "memory"); }
/// Machine-mode ECALL: exception code 11, interrupt bit clear.
inline bool is_yield(uint32_t cause) { return cause==11; }
/// Machine timer: interrupt bit 31 set plus exception code 7.
inline bool is_tick(uint32_t cause) { return cause==0x80000007u; }
/// Machine external interrupt: interrupt bit 31 set plus exception code 11.
inline bool is_external_irq(uint32_t cause) { return cause==0x8000000bu; }
/// Skip only ECALL's four bytes; a timer trap retains the interrupted PC.
inline void resume_after_yield(uint32_t* frame) { frame[32]+=4; }
}
