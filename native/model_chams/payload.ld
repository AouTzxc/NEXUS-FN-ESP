ENTRY(entry)
SECTIONS { . = 0; .text : { *(.text.entry) *(.text*) } .rodata : { *(.rodata*) } . = 0x2400; .data : { *(.data.state) *(.data*) *(.bss*) } /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }
