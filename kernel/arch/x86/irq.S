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

/* Generic hardware IRQ entry stubs for PIC lines 1-15. Each saves state and
 * calls the C dispatcher, which runs the registered handler and sends EOI.
 * These stubs never reschedule; handlers only mark threads runnable. */
.extern irq_dispatch
.irp line, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
.global x86_irq_line\line
x86_irq_line\line:
    cld
    pushal
    pushl $\line
    call irq_dispatch
    addl $4, %esp
    popal
    iret
.endr

.section .rodata
.align 4
.global x86_irq_stub_table
x86_irq_stub_table:
    .long x86_irq0
    .long x86_irq_line1, x86_irq_line2, x86_irq_line3, x86_irq_line4
    .long x86_irq_line5, x86_irq_line6, x86_irq_line7, x86_irq_line8
    .long x86_irq_line9, x86_irq_line10, x86_irq_line11, x86_irq_line12
    .long x86_irq_line13, x86_irq_line14, x86_irq_line15
