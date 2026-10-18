.section .text.start
.global _start
.option norelax
_start:
 csrci mstatus,8
 csrw mie,zero
 la sp,_stack_top
 la gp,__global_pointer$
 la t0,_bss_start
 la t1,_bss_end
1: beq t0,t1,2f
 sw zero,0(t0)
 addi t0,t0,4
 j 1b
2: call main
3: wfi
 j 3b
