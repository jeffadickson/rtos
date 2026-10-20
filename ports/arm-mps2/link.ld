/* Deliberately bounded subsets of the QEMU AN385 SSRAM regions; not a VORAGO map. */
ENTRY(Reset_Handler)
PROVIDE(__boot_stack_bytes = 4096);
MEMORY { CODE (rx) : ORIGIN = 0, LENGTH = 256K
         RAM (rw) : ORIGIN = 0x20000000, LENGTH = 64K }
SECTIONS {
 .vectors : { KEEP(*(.vectors)) } > CODE
 .text : { *(.text*) *(.rodata*) } > CODE
 .ARM.extab : { *(.ARM.extab*) } > CODE
 .ARM.exidx : { *(.ARM.exidx*) } > CODE
 .data : { *(.data*) } > RAM
 .bss (NOLOAD) : {
   . = ALIGN(16); _bss_start = .; *(.bss*) *(COMMON)
   . = ALIGN(16); _bss_end = .;
 } > RAM
 .init_array : { KEEP(*(.init_array .init_array.* .ctors .ctors.*)) } > RAM
 .preinit_array : { KEEP(*(.preinit_array .preinit_array.*)) } > RAM
 .fini_array : { KEEP(*(.fini_array .fini_array.* .dtors .dtors.*)) } > RAM
 ASSERT(SIZEOF(.init_array) == 0, "Runtime C++ constructors are unsupported; use constant initialization")
 ASSERT(SIZEOF(.preinit_array) == 0, "Runtime preinitialization is unsupported")
 ASSERT(SIZEOF(.fini_array) == 0, "Runtime finalization is unsupported")
 _stack_top = ORIGIN(RAM) + LENGTH(RAM);
 _boot_stack_bottom = _stack_top - __boot_stack_bytes;
 ASSERT(__boot_stack_bytes >= 512 && __boot_stack_bytes <= LENGTH(RAM) && (__boot_stack_bytes % 16) == 0, "Invalid boot stack reserve")
 ASSERT(_bss_end <= _boot_stack_bottom, "Static RAM overlaps exception/boot stack")
}
