/* Machine-mode RV32IM/Zicsr trap entry. See the Sphinx assembly-port chapter.
 * There is no C/C++ prologue: every interrupted integer register must survive.
 * The frame is 36 words / 144 bytes; xN occupies byte offset 4*N, except x0 is
 * unused. mepc is at 128, mstatus at 132, and 136..143 are alignment padding.
 * initialize_context() in rtos_port.hpp must construct exactly this layout.
 * Traps use the interrupted stack; the boot stack is used before first dispatch.
 * Hardware clears MIE on entry. Nested traps, FP/vector state and TLS are not
 * supported. This handler must never enable interrupts before mret.
 */
.section .text                 /* Executable code retained through the vector reference. */
.balign 4                     /* Direct mtvec requires the low two address bits clear. */
.global rtos_vector           /* Referenced by port::install_vector(). */
rtos_vector:
 addi sp,sp,-144              /* Reserve a frame, preserving 16-byte ABI alignment. */
 sw x1,4(sp)                  /* ra: interrupted return address, before call overwrites it. */
 sw x3,12(sp)                 /* gp: small-data global pointer. */
 sw x4,16(sp)                 /* tp: preserve the register even though TLS is unsupported. */
 sw x5,20(sp)                 /* t0: save before using it as the entry scratch register. */
 sw x6,24(sp)                 /* t1: caller-saved temporary. */
 sw x7,28(sp)                 /* t2: caller-saved temporary. */
 sw x8,32(sp)                 /* s0/fp: callee-saved register / optional frame pointer. */
 sw x9,36(sp)                 /* s1: callee-saved register. */
 sw x10,40(sp)                /* a0: interrupted argument/result, not the handler argument. */
 sw x11,44(sp)                /* a1: interrupted argument/result. */
 sw x12,48(sp)                /* a2: interrupted argument. */
 sw x13,52(sp)                /* a3: interrupted argument. */
 sw x14,56(sp)                /* a4: interrupted argument. */
 sw x15,60(sp)                /* a5: interrupted argument. */
 sw x16,64(sp)                /* a6: interrupted argument. */
 sw x17,68(sp)                /* a7: interrupted argument. */
 sw x18,72(sp)                /* s2: callee-saved register. */
 sw x19,76(sp)                /* s3: callee-saved register. */
 sw x20,80(sp)                /* s4: callee-saved register. */
 sw x21,84(sp)                /* s5: callee-saved register. */
 sw x22,88(sp)                /* s6: callee-saved register. */
 sw x23,92(sp)                /* s7: callee-saved register. */
 sw x24,96(sp)                /* s8: callee-saved register. */
 sw x25,100(sp)               /* s9: callee-saved register. */
 sw x26,104(sp)               /* s10: callee-saved register. */
 sw x27,108(sp)               /* s11: callee-saved register. */
 sw x28,112(sp)               /* t3: caller-saved temporary. */
 sw x29,116(sp)               /* t4: caller-saved temporary. */
 sw x30,120(sp)               /* t5: caller-saved temporary. */
 sw x31,124(sp)               /* t6: caller-saved temporary. */
 addi t0,sp,144               /* Recover x2/sp as it was before frame reservation. */
 sw t0,8(sp)                  /* Store the original sp in x2's canonical slot. */
 csrr t0,mepc                 /* Hardware continuation PC (the ECALL address for yield). */
 sw t0,128(sp)                /* C++ advances this word by four only for ECALL. */
 csrr t0,mstatus              /* Trap-entry MIE=0; MPIE retains the prior interrupt state. */
 sw t0,132(sp)                /* Keep this thread's return mode and interrupt state. */
 mv a0,sp                     /* C ABI argument 0: interrupted frame pointer. */
 csrr a1,mcause               /* C ABI argument 1: complete cause, including interrupt bit. */
 call rtos_trap               /* Save/account/select; returns the next frame in a0. */
 mv sp,a0                     /* Switch stacks only after the C++ call has returned. */
 lw t0,128(sp)                /* Read the selected thread's continuation PC. */
 csrw mepc,t0                 /* Set mret destination before restoring its temporary. */
 lw t0,132(sp)                /* Read the selected thread's saved machine status. */
 csrw mstatus,t0              /* Saved MIE stays zero until the final mret. */
 lw x1,4(sp)                  /* Restore ra; do not return through the C++ call's ra. */
 lw x3,12(sp)                 /* Restore gp. */
 lw x4,16(sp)                 /* Restore tp. */
 lw x5,20(sp)                 /* Restore t0 after all scratch CSR operations finish. */
 lw x6,24(sp)                 /* Restore t1. */
 lw x7,28(sp)                 /* Restore t2. */
 lw x8,32(sp)                 /* Restore s0/fp. */
 lw x9,36(sp)                 /* Restore s1. */
 lw x10,40(sp)                /* Restore a0, replacing the handler's frame result. */
 lw x11,44(sp)                /* Restore a1. */
 lw x12,48(sp)                /* Restore a2. */
 lw x13,52(sp)                /* Restore a3. */
 lw x14,56(sp)                /* Restore a4. */
 lw x15,60(sp)                /* Restore a5. */
 lw x16,64(sp)                /* Restore a6. */
 lw x17,68(sp)                /* Restore a7. */
 lw x18,72(sp)                /* Restore s2. */
 lw x19,76(sp)                /* Restore s3. */
 lw x20,80(sp)                /* Restore s4. */
 lw x21,84(sp)                /* Restore s5. */
 lw x22,88(sp)                /* Restore s6. */
 lw x23,92(sp)                /* Restore s7. */
 lw x24,96(sp)                /* Restore s8. */
 lw x25,100(sp)               /* Restore s9. */
 lw x26,104(sp)               /* Restore s10. */
 lw x27,108(sp)               /* Restore s11. */
 lw x28,112(sp)               /* Restore t3. */
 lw x29,116(sp)               /* Restore t4. */
 lw x30,120(sp)               /* Restore t5. */
 lw x31,124(sp)               /* Restore t6. */
 lw sp,8(sp)                  /* Restore sp LAST: every earlier load uses the frame base. */
 mret                         /* Resume mepc in M mode; MPIE becomes MIE atomically. */
