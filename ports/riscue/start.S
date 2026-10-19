/* Reset entry for the flat RAM image. The loader must place .text/.rodata/.data
 * at their linked addresses; there is no ROM-to-RAM data copy or constructor
 * runner. link.ld supplies aligned BSS bounds and the reserved boot stack top.
 * Entry must already be in machine mode on the sole participating hart.
 * See the Sphinx assembly-port chapter for boot and debugger contracts.
 */
.section .text.start          /* link.ld KEEP places startup first in the RAM image. */
.global _start                /* ELF entry symbol selected by ENTRY(_start). */
.option norelax               /* Keep gp initialization from relaxing to mv gp,gp. */
_start:
 csrci mstatus,8              /* Clear global MIE before touching uninitialized state. */
 csrw mie,zero                /* Disable all individual machine interrupt sources. */
 la sp,_stack_top             /* Use the linker-reserved 4 KiB, 16-byte-aligned boot stack. */
 la gp,__global_pointer$      /* Establish the ABI small-data base before C++ executes. */
 la t0,_bss_start             /* First word to zero; no C++ globals may be read earlier. */
 la t1,_bss_end               /* Exclusive end, padded to 16 bytes by link.ld. */
1: beq t0,t1,2f               /* Empty or fully cleared BSS proceeds to main. */
 sw zero,0(t0)                /* Initialize one aligned word of static zero-fill storage. */
 addi t0,t0,4                 /* Advance exactly one RV32 word. */
 j 1b                        /* Numeric local label 1 backward: continue BSS clearing. */
2: call main                 /* Boot registration and start(); no argc/argv runtime. */
3: wfi                       /* If main returns, remain here with interrupt sources off. */
 j 3b                        /* Also safe if WFI is implemented as a no-op. */
