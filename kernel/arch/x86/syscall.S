.code32
.section .text
.global x86_syscall_entry
.extern syscall_dispatch
x86_syscall_entry:
    cld
    pushal
    pushl %esp
    call syscall_dispatch
    addl $4, %esp
    popal
    iret
