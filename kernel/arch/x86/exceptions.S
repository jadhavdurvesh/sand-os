/* 32-bit x86 exception entry stubs. CPU-error-code vectors: 8,10-14,17,21,29,30. */
.code32
.section .text
.extern x86_exception_dispatch
.macro ISR_NOERR number
.global x86_isr_\number
x86_isr_\number:
    pushl $0
    pushl $\number
    jmp x86_exception_common
.endm
.macro ISR_ERR number
.global x86_isr_\number
x86_isr_\number:
    pushl $\number
    jmp x86_exception_common
.endm
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR 8
ISR_NOERR 9
ISR_ERR 10
ISR_ERR 11
ISR_ERR 12
ISR_ERR 13
ISR_ERR 14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR 29
ISR_ERR 30
ISR_NOERR 31

x86_exception_common:
    cld
    pushal
    leal 32(%esp), %eax
    pushl 4(%eax)
    pushl (%eax)
    call x86_exception_dispatch
    addl $8, %esp
    popal
    addl $8, %esp
    iret
.section .rodata
.align 4
.global x86_exception_stub_table
x86_exception_stub_table:
    .long x86_isr_0
    .long x86_isr_1
    .long x86_isr_2
    .long x86_isr_3
    .long x86_isr_4
    .long x86_isr_5
    .long x86_isr_6
    .long x86_isr_7
    .long x86_isr_8
    .long x86_isr_9
    .long x86_isr_10
    .long x86_isr_11
    .long x86_isr_12
    .long x86_isr_13
    .long x86_isr_14
    .long x86_isr_15
    .long x86_isr_16
    .long x86_isr_17
    .long x86_isr_18
    .long x86_isr_19
    .long x86_isr_20
    .long x86_isr_21
    .long x86_isr_22
    .long x86_isr_23
    .long x86_isr_24
    .long x86_isr_25
    .long x86_isr_26
    .long x86_isr_27
    .long x86_isr_28
    .long x86_isr_29
    .long x86_isr_30
    .long x86_isr_31
