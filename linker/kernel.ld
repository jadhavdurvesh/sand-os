ENTRY(_start)
SECTIONS
{
    . = 0x10000;
    .text ALIGN(16) : { *(.text*) }
    .rodata ALIGN(16) : { *(.rodata*) }
    .data ALIGN(16) : { *(.data*) }
    .bss ALIGN(16) : { *(COMMON) *(.bss*) }
    . = ALIGN(4096);
    __user_text_start = .;
    .usertext : { KEEP(*(.usertext)) }
    . = ALIGN(4096);
    __user_text_end = .;
}
