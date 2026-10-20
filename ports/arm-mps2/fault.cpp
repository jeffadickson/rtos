/** @file
 * @brief Default QEMU-only fault sink for applications without a custom report.
 * No runtime state is required, so this also works before BSS initialization.
 */
#include <stdint.h>
/// Semihosting SYS_EXIT_EXTENDED reports failure; an application may override it.
extern "C" [[noreturn]] __attribute__((weak)) void rtos_arm_fault(){
    const uint32_t reason[]{0x20026,1};
    register uint32_t operation asm("r0")=0x20;
    register const uint32_t* argument asm("r1")=reason;
    asm volatile("bkpt 0xab" : "+r"(operation) : "r"(argument) : "memory","cc");
    for(;;)asm volatile("wfi");
}
