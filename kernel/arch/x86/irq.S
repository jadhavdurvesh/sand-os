/* IRQ0 entry: save registers, schedule, restore selected thread. */
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

/* Software reschedule gate used by yield, block and thread exit. */
.extern scheduler_reschedule
.global x86_reschedule
x86_reschedule:
    cld
    pushal
    pushl %esp
    call scheduler_reschedule
    addl $4, %esp
    movl %eax, %esp
    popal
    iret
