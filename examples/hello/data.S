// Binary assets placed in ROM. Each label becomes a C symbol (extern const uint8_t name[]).
    .section .rodata.data, "a"
    .balign 4
    .globl font_chr, font_pal, robot_chr, robot_pal
font_chr:  .incbin "build/font.chr"
font_pal:  .incbin "build/font.pal"
robot_chr: .incbin "build/robot.chr"
robot_pal: .incbin "build/robot.pal"
