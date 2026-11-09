/* SAND OS stage-2 loader.
 * BIOS E820 map is written at physical 0x5000: count at 0x5000,
 * followed by up to 32 24-byte entries at 0x5004.
 * Kernel is read from LBA 17 to physical 0x10000.
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

    call collect_memory_map

    movw $kernel_packet, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc loader_error

    /* Load the second 64 KiB kernel window from LBA 145 to 0x20000. */
    movw $0x2000, kernel_packet+6
    movl $145, kernel_packet+8
    movl $0, kernel_packet+12
    movw $kernel_packet, %si
    movb boot_drive, %dl
    movb $0x42, %ah
    int $0x13
    jc loader_error

    inb $0x92, %al
    orb $0x02, %al
    andb $0xFE, %al
    outb %al, $0x92

    lgdt gdt_descriptor
    movl %cr0, %eax
    orl $0x1, %eax
    movl %eax, %cr0
    ljmpl $0x08, $protected_entry

/* BIOS INT 15h, EAX=E820, EDX='SMAP'. */
collect_memory_map:
    pushw %ds
    pushw %es
    pushw %di
    pushw %si
    pushw %bp
    movw $0x0500, %ax
    movw %ax, %es
    movw $4, %di
    movw $0, %es:0
    xorl %ebp, %ebp
    xorl %ebx, %ebx
e820_next:
    cmpl $32, %ebp
    jae e820_done
    movl $1, %es:20(%di)
    movl $0xE820, %eax
    movl $0x534D4150, %edx
    movl $24, %ecx
    int $0x15
    jc e820_done
    cmpl $0x534D4150, %eax
    jne e820_done
    cmpl $20, %ecx
    jb e820_continue
    incw %bp
    addw $24, %di
e820_continue:
    testl %ebx, %ebx
    jne e820_next
e820_done:
    movw %bp, %es:0
    popw %bp
    popw %si
    popw %di
    popw %es
    popw %ds
    ret

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
    .word 128
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
