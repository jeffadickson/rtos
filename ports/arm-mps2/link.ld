/* Deliberately bounded subsets of the QEMU AN385 SSRAM regions; not a VORAGO map. */
ENTRY(Reset_Handler)
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
 .init_array : { KEEP(*(.init_array*)) KEEP(*(.ctors*)) } > RAM
 ASSERT(SIZEOF(.init_array) == 0, "Dynamic constructors are unsupported")
 _stack_top = ORIGIN(RAM) + LENGTH(RAM);
 _boot_stack_bottom = _stack_top - __boot_stack_bytes;
 ASSERT(_bss_end <= _boot_stack_bottom, "Static RAM overlaps exception/boot stack")
}
