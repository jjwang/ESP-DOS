MEMORY
{
  ram : org = 0x3F000000, len = 0x40000
}

SECTIONS
{
  .text : { *(.text*) } > ram
  .rodata : { *(.rodata*) } > ram
  .data : { *(.data*) } > ram
  .bss : { *(.bss*) } > ram
  /DISCARD/ : { *(*) }
}
