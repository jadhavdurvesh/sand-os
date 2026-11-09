/* SAND OS initial Multiboot2 entry.
 * GRUB enters in 32-bit protected mode. This first milestone validates
 * the boot pipeline before adding long-mode transition and the 64-bit ABI.
 */
.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH, 0
.section .multiboot2, "a"
.align 8
header_start:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long header_end - header_start
    .long -(MB2_MAGIC + MB2_ARCH + (header_end - header_start))
    .short 0
    .short 0
    .long 8
header_end:

.section .text
.code32
.global _start
.type _start, @function
.extern kernel_main
_start:
    cli
    mov $stack_top, %esp
    call kernel_main
1:
    hlt
    jmp 1b

.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:
