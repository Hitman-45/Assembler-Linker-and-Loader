.section
.text
main:
    lui x5,2
    sw x5,-4(x8)
    lui x6,3
    sw x6,-8(x8)
    lw x5,-4(x8)
    lw x6,-8(x8)
    add x7,x5,x6
    sw x7,-12(x8)
    lw x5,-4(x8)
    lw x6,-8(x8)
    sub x28,x5,x6
    sw x28,-16(x8)
    lui x29,0
end:
