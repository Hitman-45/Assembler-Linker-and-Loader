.section
.text
main:
    lui x5,0
    sw x5,-4(x8)
    lw x6,-4(x8)
    sw x6,-8(x8)
    lui x7,4
    sw x7,-12(x8)
    lw x28,-12(x8)
    sw x28,-16(x8)
    lui x29,0
    sw x29,-20(x8)
    lw x6,-20(x8)
    sw x6,-8(x8)
L0:
    lui x30,5
    sw x30,-24(x8)
    blt x6,x30,L1
    beq x0,x0,L2
L3:
    lui x31,1
    sw x31,-28(x8)
    sw x5,-4(x8)
    lw x6,-8(x8)
    lw x31,-28(x8)
    add x5,x6,x31
    sw x5,-32(x8)
    lw x6,-32(x8)
    sw x6,-8(x8)
# ---- start of spill ----
    sw x5,-32(x8)
    sw x31,-28(x8)
    sw x30,-24(x8)
    sw x29,-20(x8)
    sw x28,-16(x8)
    sw x7,-12(x8)
    sw x6,-8(x8)
# ---- end of spill ----
    beq x0,x0,L0
L1:
    lui x5,2
    sw x5,-36(x8)
    lw x7,-16(x8)
    lw x5,-36(x8)
    add x6,x7,x5
    sw x6,-40(x8)
    lw x7,-40(x8)
    sw x7,-16(x8)
# ---- start of spill ----
    sw x5,-36(x8)
    sw x7,-16(x8)
    sw x6,-40(x8)
# ---- end of spill ----
    beq x0,x0,L3
L2:
end: