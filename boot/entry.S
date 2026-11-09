/* SAND kernel entry. Control arrives here from our stage-2 loader
 * in 32-bit protected mode with flat data/code segments and paging off.
 */
.code32
.section .text
.global _start
.type _start, @function
.extern kernel_main
_start:
    cli
    movl $0x90000, %esp
    xorl %ebp, %ebp
    call kernel_main
1:
    hlt
    jmp 1b
