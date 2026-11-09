/* SAND OS stage-1 boot sector: our own 16-bit BIOS loader.
 * BIOS supplies the firmware interrupt interface; all disk-loading logic
 * and the next-stage loader are implemented in this repository.
 */
.code16
.section .text
.global _start
_start:
    jmp boot_start
    nop

boot_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    sti
    movb %dl, boot_drive

    /* Load the 8 KiB stage 2 from disk sectors 1..16 at physical 0x8000. */
    movw $disk_packet, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc boot_error
    ljmp $0x0000, $0x8000

boot_error:
    movw $error_text, %si
print_next:
    lodsb
    testb %al, %al
    jz halt
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp print_next

halt:
    cli
1:
    hlt
    jmp 1b

.align 4
disk_packet:
    .byte 0x10, 0
    .word 16
    .word 0x8000
    .word 0x0000
    .quad 1
boot_drive:
    .byte 0
error_text:
    .asciz "SAND: stage-2 disk read failed"
.org 510
.word 0xAA55
