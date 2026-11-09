/* SAND OS stage-2 loader: raw kernel loader and protected-mode switch.
 * Disk layout: LBA 0 boot sector; LBA 1..16 this 8 KiB stage; LBA 17..80
 * kernel payload. The initial milestone intentionally targets legacy BIOS.
 */
.code16
.section .text
.global stage2_start
stage2_start:
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw $0x7C00, %sp
    movb %dl, boot_drive

    /* Read 32 KiB kernel payload from LBA 17 to physical 0x10000. */
    movw $kernel_packet, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc loader_error

    /* Enable A20 using the chipset fast-gate port. */
    inb $0x92, %al
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92

    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_entry

loader_error:
    movw $error_text, %si
print_error:
    lodsb
    testb %al, %al
    jz loader_halt
    movb $0x0E, %ah
    movw $0x0007, %bx
    int $0x10
    jmp print_error
loader_halt:
    cli
1:
    hlt
    jmp 1b

.align 4
kernel_packet:
    .byte 0x10, 0
    .word 64
    .word 0x0000
    .word 0x1000
    .quad 17
boot_drive:
    .byte 0
error_text:
    .asciz "SAND: kernel disk read failed"

.align 8
gdt:
    .quad 0x0000000000000000
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_end:
gdt_descriptor:
    .word gdt_end - gdt - 1
    .long gdt

.code32
protected_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    movl $0x90000, %esp
    movl $0x10000, %eax
    jmp *%eax
