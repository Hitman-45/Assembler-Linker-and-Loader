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
L0:
