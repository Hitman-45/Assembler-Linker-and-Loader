.section
.text
main:
    lui x5,4
    sw x5,-4(x8)
    lw x6,-4(x8)
    sw x6,-8(x8)
    lui x7,1
    sw x7,-12(x8)
    beq x6,x7,L1
    beq x0,x0,L2
L1:
    lui x28,1
    sw x28,-16(x8)
    lw x6,-8(x8)
    lw x28,-16(x8)
    add x29,x6,x28
    sw x29,-20(x8)
    lw x6,-20(x8)
    sw x6,-8(x8)
# ---- start of spill ----
    sw x29,-20(x8)
    sw x7,-12(x8)
    sw x28,-16(x8)
    sw x6,-8(x8)
    sw x5,-4(x8)
# ---- end of spill ----
    beq x0,x0,L0
L2:
    lui x5,2
    sw x5,-24(x8)
    beq x6,x5,L3
    beq x0,x0,L4
L3:
    lui x7,2
    sw x7,-28(x8)
    lw x6,-8(x8)
    lw x7,-28(x8)
    add x28,x6,x7
    sw x28,-32(x8)
    lw x6,-32(x8)
    sw x6,-8(x8)
# ---- start of spill ----
    sw x7,-28(x8)
    sw x5,-24(x8)
    sw x28,-32(x8)
    sw x6,-8(x8)
# ---- end of spill ----
    beq x0,x0,L0
L4:
    lui x5,3
    sw x5,-36(x8)
    lw x7,-8(x8)
    lw x5,-36(x8)
    add x6,x7,x5
    sw x6,-40(x8)
    lw x7,-40(x8)
    sw x7,-8(x8)
L0:
    lui x28,0
    sw x28,-44(x8) #mv a0,x28 not supported
end:
