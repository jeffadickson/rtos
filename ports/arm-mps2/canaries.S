/* Simulation validation only. Each function restores its caller's AAPCS state.
 * Unreferenced functions are removed by section GC from ordinary applications.
 * Positive return=1; an intentionally injected mismatch must return 0.
 */
.syntax unified
.cpu cortex-m3
.thumb
.section .text.arm_canaries,"ax",%progbits
.macro check_register reg,value
 ldr r0,=\value
 cmp \reg,r0
 bne .Lcallee_fail
.endm
.global rtos_arm_callee_canary
.type rtos_arm_callee_canary,%function
.thumb_func
rtos_arm_callee_canary:
 push {r4-r11,lr}            /* 36 bytes; reserve four more for 8-byte ABI alignment. */
 sub sp,sp,#4
 str r0,[sp]                /* Input: zero=positive, nonzero=inject r7 mismatch. */
 ldr r4,=0x44444444
 ldr r5,=0x55555555
 ldr r6,=0x66666666
 ldr r7,=0x77777777
 ldr r8,=0x88888888
 ldr r9,=0x99999999
 ldr r10,=0xaaaaaaaa
 ldr r11,=0xbbbbbbbb
 svc 0                      /* Yield while all callee-saved values are live. */
 bl rtos_arm_canary_park     /* C++ sleeps; caller-saved registers may change here. */
 ldr r0,[sp]
 cbz r0,.Lcallee_check
 eor r7,r7,#1
.Lcallee_check:
 check_register r4,0x44444444
 check_register r5,0x55555555
 check_register r6,0x66666666
 check_register r7,0x77777777
 check_register r8,0x88888888
 check_register r9,0x99999999
 check_register r10,0xaaaaaaaa
 check_register r11,0xbbbbbbbb
 movs r0,#1
 b .Lcallee_return
.Lcallee_fail:
 movs r0,#0
.Lcallee_return:
 add sp,sp,#4
 pop {r4-r11,pc}
.size rtos_arm_callee_canary,.-rtos_arm_callee_canary
.ltorg

.global rtos_arm_irq_canary
.type rtos_arm_irq_canary,%function
.thumb_func
rtos_arm_irq_canary:
 push {r4-r11,lr}
 sub sp,sp,#76              /* 36+76=112, aligned. Snapshot and loop scratch. */
 str r0,[sp,#60]            /* Input injection flag, outside register snapshot. */
 ldr r0,=10000
 str r0,[sp,#64]
.Lirq_loop:
 ldr r0,=0x10101010
 ldr r1,=0x11111111
 ldr r2,=0x12121212
 ldr r3,=0x13131313
 ldr r4,=0x14141414
 ldr r5,=0x15151515
 ldr r6,=0x16161616
 ldr r7,=0x17171717
 ldr r8,=0x18181818
 ldr r9,=0x19191919
 ldr r10,=0x1a1a1a1a
 ldr r11,=0x1b1b1b1b
 ldr r12,=0x1c1c1c1c
 ldr lr,=0xeeeeeeee
 ldr r0,[sp,#60]
 cmp r0,#1
 bne .Lirq_flags
 eor r7,r7,#1               /* Negative probe must fail comparison. */
.Lirq_flags:
 cmp r0,#2
 beq .Lirq_bad_flags
 movs r0,#0
 subs r0,r0,#1              /* Expected NZCV=1000; no C calls until snapshot. */
 b .Lirq_ready
.Lirq_bad_flags:
 movs r0,#0
 adds r0,r0,#1              /* NZCV=0000; independent flags-negative probe. */
.Lirq_ready:
 ldr r0,=0x10101010          /* Non-flag-setting reload of the canary. */
.global rtos_arm_irq_live_begin
rtos_arm_irq_live_begin:
 .rept 64
 nop
 .endr
.global rtos_arm_irq_live_end
rtos_arm_irq_live_end:
 stmia sp,{r0-r12,lr}       /* Does not change flags; snapshot all 14 canaries. */
 mrs r0,apsr
 str r0,[sp,#56]
 adr r1,.Lexpected
 mov r2,sp
 movs r3,#14
.Lirq_compare:
 ldr r0,[r1],#4
 ldr r4,[r2],#4
 cmp r0,r4
 bne .Lirq_fail
 subs r3,r3,#1
 bne .Lirq_compare
 ldr r0,[sp,#56]
 lsrs r0,r0,#28
 cmp r0,#8
 bne .Lirq_fail
 ldr r0,[sp,#64]
 subs r0,r0,#1
 str r0,[sp,#64]
 bne .Lirq_loop
 movs r0,#1
 b .Lirq_return
.Lirq_fail:
 movs r0,#0
.Lirq_return:
 add sp,sp,#76
 pop {r4-r11,pc}
.balign 4
.Lexpected:
 .word 0x10101010,0x11111111,0x12121212,0x13131313
 .word 0x14141414,0x15151515,0x16161616,0x17171717
 .word 0x18181818,0x19191919,0x1a1a1a1a,0x1b1b1b1b
 .word 0x1c1c1c1c,0xeeeeeeee
.size rtos_arm_irq_canary,.-rtos_arm_irq_canary
.ltorg

.global rtos_arm_padding_canary
.type rtos_arm_padding_canary,%function
.thumb_func
rtos_arm_padding_canary:
 push {r4-r6,lr}            /* Preserve ABI alignment and original mask. */
 mov r4,sp
 mrs r5,basepri
 movs r0,#0x80
 msr basepri_max,r0
 dsb
 isb
 sub sp,sp,#4               /* Deliberately misalign PSP; do not call C here. */
 svc 0
 ldr r2,[sp,#-8]            /* Stale hardware xPSR below returned PSP: padding bit9. */
 add sp,sp,#4
 mov r1,sp
 movs r0,#0
 cmp r1,r4
 bne .Lpadding_return
 tst r2,#0x200
 it ne
 movne r0,#1
.Lpadding_return:
 msr basepri,r5
 dsb
 isb
 pop {r4-r6,pc}
.size rtos_arm_padding_canary,.-rtos_arm_padding_canary
