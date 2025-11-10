.section
.data
msg:
.asciz "Hello 1"
a:
.asciz "Hello 2"
b:
.asciz "Hello 3"
.section
.text
main:
    lui x5,0x10010
    addi x5,x5,0
    sw x5,-4(x8)
    lw x5,-4(x8)
    addi a0,x5,0       # mv a0,x5
    lui a7,4
    ecall               # print msg (assuming ecall is supported)
    lui x6,0x10010
    addi x6,x6,0
    addi x6,x6,8
    sw x6,-8(x8)
    lw x6,-8(x8)
    addi a0,x6,0       # mv a0,x6
    lui a7,4
    ecall               # print a
    lui x7,0x10010
    addi x7,x7,0
    addi x7,x7,16
    sw x7,-12(x8)
    lw x7,-12(x8)
    addi a0,x7,0       # mv a0,x7
    lui a7,4
    ecall               # print b
    lui x28,0
    addi a0,x28,0
    jalr x2,0(x1)        # ret
