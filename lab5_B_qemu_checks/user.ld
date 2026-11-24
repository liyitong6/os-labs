OUTPUT_ARCH(riscv)
ENTRY(fixture_user_entry)
SECTIONS {
  . = 0x00800000;
  .text : { KEEP(*(.text.entry)) *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.* .sdata .sdata.*) }
  .bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
  ASSERT(. < 0x00804000, "Fixture user image exceeds four pages")
  /DISCARD/ : { *(.eh_frame .comment .note .note.*) }
}
