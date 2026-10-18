OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY { RAM (rwx) : ORIGIN = 0, LENGTH = 0x7f00 }
SECTIONS {
 .text : { KEEP(*(.text.start)) *(.text*) } > RAM
 .rodata : { *(.rodata*) } > RAM
 .data : { . = ALIGN(16); PROVIDE(__global_pointer$ = . + 0x800); *(.data*) *(.sdata*) } > RAM
 .init_array : { KEEP(*(.init_array .init_array.* .ctors .ctors.*)) } > RAM
 .preinit_array : { KEEP(*(.preinit_array .preinit_array.*)) } > RAM
 ASSERT(SIZEOF(.init_array) == 0, "Runtime C++ constructors are unsupported; use constant initialization")
 ASSERT(SIZEOF(.preinit_array) == 0, "Runtime preinitialization is unsupported")
 .bss (NOLOAD) : { . = ALIGN(16); _bss_start = .; *(.bss*) *(.sbss*) *(COMMON) . = ALIGN(16); _bss_end = .; } > RAM
 _stack_top = ORIGIN(RAM) + LENGTH(RAM);
 ASSERT(_bss_end <= _stack_top - 0x1000, "Insufficient boot/trap stack reserve")
 /DISCARD/ : { *(.eh_frame*) *(.comment*) }
}
