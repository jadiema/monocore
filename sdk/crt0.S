// MONOCORE-1 start-up code: ROM header, C runtime init, interrupt entry
    .section .header, "a"
    .ascii "MNC1"           // 0x00 signature
    .word 1                 // 0x04 header version
    .word _start            // 0x08 entry point
    .word 0                 // 0x0C ROM size (patched by mkrom.py)
    .word 0                 // 0x10 flags: default rotation
    .word 0                 // 0x14 default DIP switches
    .word 0, 0              // 0x18
    .space 0x100 - 0x20     // title and origin (patched by mkrom.py)

    .section .text.start, "ax"
    .globl _start
_start:
    la sp, __stack_top
    csrw mcountinhibit, zero    // enable mcycle/minstret (the CPU may reset with them inhibited)
    // A reset does not clear the memories: on MiSTer, loading a new ROM leaves the previous
    // game's maps, sprites, palettes and sound registers in place. Zero them by DMA fill
    // (the CPU waits on DMA_CTRL until each fill ends). NVRAM is kept.
    la t0, clear_table
    la t4, clear_table_end
    li t5, 0x40000040           // DMA_SRC (fill pattern), then DMA_DST, DMA_LEN, DMA_CTRL
    li t3, 3                    // start, fill mode
8:  bgeu t0, t4, 9f
    lw t1, 0(t0)
    lw t2, 4(t0)
    sw zero, 0(t5)
    sw t1, 4(t5)
    sw t2, 8(t5)
    sw t3, 12(t5)
    addi t0, t0, 8
    j 8b
9:  // copy .data from ROM to WRAM
    la t0, __data_load
    la t1, __data_start
    la t2, __data_end
1:  bgeu t1, t2, 2f
    lw t3, 0(t0)
    sw t3, 0(t1)
    addi t0, t0, 4
    addi t1, t1, 4
    j 1b
2:  // clear .bss
    la t1, __bss_start
    la t2, __bss_end
3:  bgeu t1, t2, 4f
    sw zero, 0(t1)
    addi t1, t1, 4
    j 3b
4:  la t0, trap_entry
    csrw mtvec, t0
    call main
5:  wfi
    j 5b

    .section .rodata
    .align 2
clear_table:                    // address, length in bytes
    .word 0x30000000, 0x100     // PCM voices (sound first: the previous game's notes stop at once)
    .word 0x30000400, 0x80      // PSG channels
    .word 0x20010000, 0x2000    // palette
    .word 0x20020000, 0x2000    // sprite table
    .word 0x20040000, 0x10000   // tilemaps
    .word 0x20080000, 0x2000    // line scroll tables
    .word 0x200C0000, 0x20000   // CHR RAM
    .word 0x10000000, 0x20000   // WRAM (the stack is not in use yet)
clear_table_end:

    .text
    .align 2
trap_entry:
    addi sp, sp, -64
    sw ra, 0(sp)
    sw t0, 4(sp)
    sw t1, 8(sp)
    sw t2, 12(sp)
    sw a0, 16(sp)
    sw a1, 20(sp)
    sw a2, 24(sp)
    sw a3, 28(sp)
    sw a4, 32(sp)
    sw a5, 36(sp)
    sw a6, 40(sp)
    sw a7, 44(sp)
    sw t3, 48(sp)
    sw t4, 52(sp)
    sw t5, 56(sp)
    sw t6, 60(sp)
    csrr t0, mcause
    bltz t0, 6f             // interrupt
    // synchronous exception: report and stop
    li t1, 0x40000060
    li t2, 'X'
    sw t2, 0(t1)
7:  j 7b
6:  call irq_handler
    lw ra, 0(sp)
    lw t0, 4(sp)
    lw t1, 8(sp)
    lw t2, 12(sp)
    lw a0, 16(sp)
    lw a1, 20(sp)
    lw a2, 24(sp)
    lw a3, 28(sp)
    lw a4, 32(sp)
    lw a5, 36(sp)
    lw a6, 40(sp)
    lw a7, 44(sp)
    lw t3, 48(sp)
    lw t4, 52(sp)
    lw t5, 56(sp)
    lw t6, 60(sp)
    addi sp, sp, 64
    mret
