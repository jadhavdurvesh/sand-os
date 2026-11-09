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
    movl 4(%esp), %eax
    movl 8(%esp), %edx
    movw $0x23, %cx
    movw %cx, %ds
    movw %cx, %es
    movw %cx, %fs
    movw %cx, %gs
    pushl $0x23
    pushl %edx
    pushfl
    popl %ecx
    orl $0x200, %ecx
    pushl %ecx
    pushl $0x1B
    pushl %eax
    iret
