/* Simulation startup: QEMU ELF loader installs text/data at their linked VMAs.
 * No ROM copying, constructors, libc startup or heap. The CPU reads the vector
 * table at zero. Every exception except SVC/SysTick fails through the test sink.
 */
.syntax unified
.cpu cortex-m3
.thumb
.section .vectors,"a",%progbits
.balign 256
.global arm_vectors
arm_vectors:
 .word _stack_top            /* Initial MSP, reserved by the linker. */
 .word Reset_Handler         /* Thumb reset entry. */
 .rept 9
 .word rtos_arm_fault        /* NMI, faults and reserved core vectors fail closed. */
 .endr
 .word SVC_Handler           /* Exception 11: synchronous yield/block. */
 .word rtos_arm_fault        /* DebugMonitor is unsupported. */
 .word 0                     /* Reserved. */
 .word rtos_arm_fault        /* PendSV is deliberately unused by this port. */
 .word SysTick_Handler       /* Exception 15: preemption and deadlines. */
 .rept 32
 .word rtos_arm_fault        /* Peripheral IRQs are not enabled by this BSP. */
 .endr
.section .text.start,"ax",%progbits
.global Reset_Handler
.type Reset_Handler,%function
.thumb_func
Reset_Handler:
 cpsid i                     /* Exclude configurable exceptions during BSS initialization. */
 ldr r0,=_bss_start
 ldr r1,=_bss_end
 movs r2,#0
1: cmp r0,r1
 bhs 2f
 str r2,[r0],#4              /* Clear complete aligned zero-fill static storage. */
 b 1b
2: cpsie i                   /* Leave PRIMASK clear; BASEPRI is the kernel mask. */
 bl main                     /* Register permanent objects, then start scheduling. */
 b rtos_arm_fault            /* main returning is a failed simulation, never success. */
