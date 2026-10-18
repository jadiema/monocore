/* MONOCORE-1 linker script: code and constants in ROM, data/bss/stack in WRAM */
OUTPUT_ARCH(riscv)
ENTRY(_start)
MEMORY {
  ROM  (rx)  : ORIGIN = 0x00000000, LENGTH = 32M
  WRAM (rwx) : ORIGIN = 0x10000000, LENGTH = 128K
}
SECTIONS {
  .header : { KEEP(*(.header)) } > ROM
  .text 0x100 : { KEEP(*(.text.start)) *(.text .text.*) } > ROM
  .rodata : ALIGN(4) { *(.rodata .rodata.* .srodata .srodata.*) } > ROM
  .data : ALIGN(4) {
    __data_start = .;
    *(.data .data.* .sdata .sdata.*)
    . = ALIGN(4);
    __data_end = .;
  } > WRAM AT > ROM
  __data_load = LOADADDR(.data);
  .bss (NOLOAD) : ALIGN(4) {
    __bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    . = ALIGN(4);
    __bss_end = .;
  } > WRAM
  __stack_top = ORIGIN(WRAM) + LENGTH(WRAM);
  /DISCARD/ : { *(.comment) *(.riscv.attributes) *(.note*) *(.eh_frame*) }
}
