
stp x29, x30, [sp, #-64]!
mov x29, sp
stp x19, x20, [sp, #16]
stp x21, x22, [sp, #32]
mov x19, x0
mov x20, x8
mov w22, w1
movz x21, #0
movk x21, #0, lsl #16
movk x21, #0, lsl #32
movk x21, #0, lsl #48
ldr x16, [x21]
blr x16
ldr x9, [x21, #8]
cmp x19, x9
b.ne finished
ldr w9, [x21, #16]
add w9, w9, #1
str w9, [x21, #16]
str w22, [x21, #24]
ldp d0, d1, [x20, #32]
ldr d2, [x20, #48]
stp d0, d1, [x21, #80]
str d2, [x21, #96]
ldp q6, q7, [x20]
stp q6, q7, [x21, #128]
ldr w9, [x21, #28]
cbz w9, finished
sub w9, w9, #1
str w9, [x21, #28]
add x12, x21, #192
ldar w10, [x12]
tbnz w10, #0, finished
cbz w10, finished
ldp d3, d4, [x21, #104]
ldr d5, [x21, #120]
ldr w11, [x21, #16]
ldr w13, [x21, #232]
sub w11, w11, w13
ldr w13, [x21, #236]
cmp w11, w13
b.hi finished
ucvtf d7, w11
ldr d6, [x21, #208]
fmadd d3, d6, d7, d3
ldr d6, [x21, #216]
fmadd d4, d6, d7, d4
ldr d6, [x21, #224]
fmadd d5, d6, d7, d5
dmb ishld
ldr w11, [x12]
cmp w10, w11
b.ne finished
fsub d3, d3, d0
fsub d4, d4, d1
fsub d5, d5, d2
fmul d6, d3, d3
fmadd d6, d4, d4, d6
fmadd d6, d5, d5, d6
fsqrt d6, d6
fcmp d6, d6
b.vs finished
fmov d7, #1.0
fcmp d6, d7
b.lt finished
ldr d7, [x21, #200]
fcmp d6, d7
b.gt finished
fadd d3, d6, d3
fmul d6, d3, d3
fmadd d6, d4, d4, d6
fmadd d6, d5, d5, d6
fsqrt d6, d6
fcmp d6, #0.0
b.eq antiparallel
fdiv d1, d5, d6
fneg d1, d1
fdiv d2, d4, d6
fdiv d3, d3, d6
fmov d0, xzr
b store_quat
antiparallel:
fmov d0, xzr
fmov d1, xzr
fmov d2, #1.0
fmov d3, xzr
store_quat:
stp d0, d1, [x20]
stp d2, d3, [x20, #16]
stp d0, d1, [x21, #160]
stp d2, d3, [x21, #176]
ldr w9, [x21, #20]
add w9, w9, #1
str w9, [x21, #20]
finished:
ldp x21, x22, [sp, #32]
ldp x19, x20, [sp, #16]
ldp x29, x30, [sp], #64
ret
