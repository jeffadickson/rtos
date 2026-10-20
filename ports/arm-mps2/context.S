/* Cortex-M3 integer context switch, QEMU MPS2 AN385 only.
 * Hardware saves r0-r3,r12,lr,pc,xPSR on PSP (MSP for initial bootstrap).
 * This handler prefixes BASEPRI, EXC_RETURN and r4-r11: 18 words / 72 bytes.
 * Handler C++ uses the separate boot/MSP stack; application frames stay on PSP.
 * SVC priority 0 permits synchronous blocking with BASEPRI=0x80; SysTick is
 * priority 0x80. Kernel handlers do not nest. External IRQs remain disabled.
 * No FP, privilege changes, TLS or arbitrary nested exception support exists.
 */
.syntax unified
.cpu cortex-m3
.thumb
.section .text.arm_context,"ax",%progbits
.global SVC_Handler
.global SysTick_Handler
.type SVC_Handler,%function
.thumb_func
SVC_Handler:
 movs r1,#11                 /* rtos_trap cause: explicit yield/block. */
 b arm_context               /* LR still holds architectural EXC_RETURN. */
.type SysTick_Handler,%function
.thumb_func
SysTick_Handler:
 movs r1,#15                 /* rtos_trap cause: timer tick. */
arm_context:
 tst lr,#4                   /* EXC_RETURN bit 2 selects the interrupted stack. */
 ite eq
 mrseq r0,msp                /* Initial SVC comes from bootstrap on MSP. */
 mrsne r0,psp                /* All running application threads use PSP. */
 stmdb r0!,{r4-r11}          /* Save callee-saved registers before C++ can use them. */
 mrs r2,basepri              /* Preserve this suspended call's critical-section mask. */
 mov r3,lr                   /* Preserve thread return mode/basic-frame selection. */
 stmdb r0!,{r2,r3}           /* Slots 0/1; r4..r11 now occupy slots 2..9. */
 tst lr,#4
 it eq
 msreq msp,r0                /* Keep bootstrap frame clear of the C++ handler stack. */
 movs r2,#0x80
 msr basepri_max,r2          /* Serialize scheduler state while selecting a frame. */
 dsb
 isb
 push {r3,lr}                /* Preserve 8-byte AAPCS alignment on MSP. */
 bl rtos_trap                /* r0=frame, r1=cause; result r0=selected frame. */
 pop {r3,lr}                 /* C++ stack has fully unwound before changing PSP. */
 ldmia r0!,{r2,r3}           /* Selected BASEPRI and EXC_RETURN. */
 ldmia r0!,{r4-r11}          /* Restore all software-saved application registers. */
 msr psp,r0                  /* PSP now points to the selected hardware frame. */
 msr basepri,r2              /* Restore mask; current handler priority prevents ticks. */
 dsb
 isb
 bx r3                       /* Exception return restores hardware state and selects PSP. */
