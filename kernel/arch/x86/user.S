.code32
.section .usertext,"ax"
.global x86_user_test_entry
x86_user_test_entry:
    movl $1, %eax
    movl $0x53414E44, %ebx
    int $0x80
1:
    jmp 1b
