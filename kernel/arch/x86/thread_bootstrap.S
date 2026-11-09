/* First instruction for a newly-created kernel thread. */
.code32
.section .text
.extern scheduler_thread_bootstrap
.global x86_thread_bootstrap
x86_thread_bootstrap:
    call scheduler_thread_bootstrap
1:
    cli
    hlt
    jmp 1b
