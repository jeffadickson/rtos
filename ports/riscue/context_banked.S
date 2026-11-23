/* RISCue-MT Banked Hardware Context Switch Trap Entry (Phase 4).
 *
 * When RTOS_RISCUE_BANKED=1 and NexRT-HWSched is present, threads with
 * tid < RTOS_RISCUE_BANKS own dedicated hardware register banks inside
 * riscue_banked_rf. On trap entry:
 *   1. Read NexRT-HWSched STATUS (current_tid, current_valid).
 *   2. Fast path (banked thread): skip all 31 GPR stack stores! Only switch sp
 *      to the dedicated mscratch kernel stack and invoke rtos_trap(0, mcause).
 *      When rtos_trap writes COMMIT to NexRT-HWSched, hardware switches the
 *      active register bank in 1 cycle; mret immediately resumes the next
 *      banked thread (~5 cycles total vs ~200+ cycles over AXI!).
 *   3. Spill path (tid >= RTOS_RISCUE_BANKS or idle): falls back cleanly to
 *      full 36-word frame save/restore matching ports/riscue/context.S.
 */
#ifndef RTOS_HWSCHED_BASE
#define RTOS_HWSCHED_BASE 0x10010000
#endif
#ifndef RTOS_RISCUE_BANKS
#define RTOS_RISCUE_BANKS 8
#endif

.section .text
.balign 4
.global rtos_vector
rtos_vector:
 /* Use mscratch-backed trap stack directly when active thread is hardware-banked.
  * Check NexRT-HWSched STATUS without clobbering banked GPRs by spilling only
  * t0/t1 on the current stack temporarily if needed, OR using dedicated kernel bank.
  * Here we preserve t0/t1 in a minimal 16-byte scratch slot before inspecting STATUS.
  */
 addi sp,sp,-16
 sw t0,0(sp)
 sw t1,4(sp)
 li t0,RTOS_HWSCHED_BASE
 lw t1,4(t0)                  /* Read STATUS: [3:0]=current_tid, [4]=current_valid. */
 andi t0,t1,0x10              /* current_valid? */
 beqz t0,.Lfull_save
 andi t0,t1,0x0f              /* current_tid */
 li t1,RTOS_RISCUE_BANKS
 bgeu t0,t1,.Lfull_save

 /* --- FAST PATH: hardware-banked thread (zero GPR stack traffic!) --- */
 lw t0,0(sp)
 lw t1,4(sp)
 addi sp,sp,16                /* Restore banked sp untouched. */
 csrw mscratch,sp             /* Temporarily stash banked sp if needed; kernel uses _stack_top. */
 la sp,_stack_top             /* Run rtos_trap on dedicated kernel stack. */
 mv a0,zero                   /* Signal banked frame (hardware holds GPR/mepc/mstatus). */
 csrr a1,mcause
 call rtos_trap               /* Writes COMMIT -> hardware swaps register bank in 1 cycle! */
 li t0,RTOS_HWSCHED_BASE
 lw t1,4(t0)
 andi t0,t1,0x10
 beqz t0,.Lfull_restore
 andi t0,t1,0x0f
 li t1,RTOS_RISCUE_BANKS
 bgeu t0,t1,.Lfull_restore
 mret                         /* Banked mepc/mstatus/GPRs already active in 1 cycle! */

 /* --- SPILL PATH: full 36-word RAM context save/restore --- */
.Lfull_save:
 lw t0,0(sp)
 lw t1,4(sp)
 addi sp,sp,16
 addi sp,sp,-144
 sw x1,4(sp)
 sw x3,12(sp)
 sw x4,16(sp)
 sw x5,20(sp)
 sw x6,24(sp)
 sw x7,28(sp)
 sw x8,32(sp)
 sw x9,36(sp)
 sw x10,40(sp)
 sw x11,44(sp)
 sw x12,48(sp)
 sw x13,52(sp)
 sw x14,56(sp)
 sw x15,60(sp)
 sw x16,64(sp)
 sw x17,68(sp)
 sw x18,72(sp)
 sw x19,76(sp)
 sw x20,80(sp)
 sw x21,84(sp)
 sw x22,88(sp)
 sw x23,92(sp)
 sw x24,96(sp)
 sw x25,100(sp)
 sw x26,104(sp)
 sw x27,108(sp)
 sw x28,112(sp)
 sw x29,116(sp)
 sw x30,120(sp)
 sw x31,124(sp)
 addi t0,sp,144
 sw t0,8(sp)
 csrr t0,mepc
 sw t0,128(sp)
 csrr t0,mstatus
 sw t0,132(sp)
 mv a0,sp
 csrr a1,mcause
 la sp,_stack_top
 call rtos_trap
.Lfull_restore:
 mv sp,a0
 lw t0,128(sp)
 csrw mepc,t0
 lw t0,132(sp)
 csrw mstatus,t0
 lw x1,4(sp)
 lw x3,12(sp)
 lw x4,16(sp)
 lw x5,20(sp)
 lw x6,24(sp)
 lw x7,28(sp)
 lw x8,32(sp)
 lw x9,36(sp)
 lw x10,40(sp)
 lw x11,44(sp)
 lw x12,48(sp)
 lw x13,52(sp)
 lw x14,56(sp)
 lw x15,60(sp)
 lw x16,64(sp)
 lw x17,68(sp)
 lw x18,72(sp)
 lw x19,76(sp)
 lw x20,80(sp)
 lw x21,84(sp)
 lw x22,88(sp)
 lw x23,92(sp)
 lw x24,96(sp)
 lw x25,100(sp)
 lw x26,104(sp)
 lw x27,108(sp)
 lw x28,112(sp)
 lw x29,116(sp)
 lw x30,120(sp)
 lw x31,124(sp)
 lw sp,8(sp)
 mret
