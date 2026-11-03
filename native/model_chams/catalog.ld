ENTRY(entry)
SECTIONS {
 . = 0;
 .text : { *(.text.entry) *(.text*) }
 .rodata : { *(.rodata*) }
 ASSERT(. <= 0x4000, "catalog code exceeds reserved executable span")
 . = 0x4000;
 .data : { *(.data.state) *(.data*) *(.bss*) }
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
