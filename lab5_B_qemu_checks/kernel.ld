OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS {
  . = 0x80200000;
  .text : { KEEP(*(.text.entry)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.* .sdata .sdata.*) }
  . = ALIGN(4096);
  .user_image : { KEEP(*(.user_image)) }
  . = ALIGN(16);
  __bss_start = .;
  .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* COMMON) }
  . = ALIGN(16);
  __bss_end = .;
  /DISCARD/ : { *(.eh_frame .comment .note .note.*) }
}
