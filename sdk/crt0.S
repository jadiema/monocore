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
    // copy .data from ROM to WRAM
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
