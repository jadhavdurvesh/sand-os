/* Timer IRQ saves the interrupted register frame, then switches stacks. */
.code32
.section .text
.extern timer_irq_handler
.global x86_irq0
x86_irq0:
    cld
    pushal
    pushl %esp
    call timer_irq_handler
    addl $4, %esp
    movl %eax, %esp
    popal
    iret
