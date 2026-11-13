.code32
.section .text
.global x86_syscall_entry
.extern syscall_dispatch
/* int 0x80 entry. The dispatcher returns the stack to resume, which differs
 * from the entry stack when the call exits, blocks or yields. */
x86_syscall_entry:
    cld
    pushal
    pushl %esp
    call syscall_dispatch
    addl $4, %esp
    movl %eax, %esp
    popal
    iret
