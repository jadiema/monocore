    .section .rodata.blob, "a"
    .balign 64
    .globl blob
blob:
    .incbin "blob.bin"
