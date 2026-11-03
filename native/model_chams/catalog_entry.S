.section .text.entry,"ax",@progbits
.macro WRAPPER name, original_offset
.global \name
.type \name,%function
\name:
 stp x29,x30,[sp,#-32]!
 stp x19,x20,[sp,#16]
 mov x29,sp
 mov x19,x0
 adrp x20,state
 add x20,x20,:lo12:state
 ldr x16,[x20,#\original_offset]
 blr x16
 mov x0,x20
 mov x1,x19
 bl update_catalog
 ldp x19,x20,[sp,#16]
 ldp x29,x30,[sp],#32
 ret
.endm
WRAPPER entry,0
WRAPPER entry_second,8
