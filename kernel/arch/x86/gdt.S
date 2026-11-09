.code32
.section .text
.global x86_gdt_flush
x86_gdt_flush:
    movl 4(%esp), %eax
    lgdt (%eax)
    ljmp $0x08, $1f
1:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    movw %ax, %ss
    ret

.global x86_tss_load
x86_tss_load:
    movw 4(%esp), %ax
    ltr %ax
    ret

.global x86_enter_user
x86_enter_user:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    pushl $0x23
    pushl 8(%esp)
    pushfl
    popl %eax
    orl $0x200, %eax
    pushl %eax
    pushl $0x1B
    pushl 4(%esp)
    iret
