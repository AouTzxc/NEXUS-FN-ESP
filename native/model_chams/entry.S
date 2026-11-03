.section .text.entry,"ax",@progbits
.global entry
.type entry,%function
entry:
 stp x29,x30,[sp,#-32]!
 stp x19,x20,[sp,#16]
 mov x29,sp
 mov x19,x0
 adrp x20,state
 add x20,x20,:lo12:state
 ldr x16,[x20]
 blr x16
 mov x0,x20
 mov x1,x19
 bl update_proxy
 ldp x19,x20,[sp,#16]
 ldp x29,x30,[sp],#32
 ret
