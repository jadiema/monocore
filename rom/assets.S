// pulls the generated asset blob into ROM
    .section .rodata.assets, "a"
    .balign 64
    .globl assets
assets:
    .incbin "build/assets.bin"
