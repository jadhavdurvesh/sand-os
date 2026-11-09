/* SAND IRQ entry points (32-bit protected mode). */
.code32
.section .text
.extern timer_irq_handler
.global x86_irq0
x86_irq0:
    cld
    pushal
    call timer_irq_handler
    popal
    iret
