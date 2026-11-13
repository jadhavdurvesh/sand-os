#ifndef SAND_KERNEL_SCHEDULER_H
#define SAND_KERNEL_SCHEDULER_H
#include <stdint.h>
#define SAND_SCHEDULER_MAX_THREADS 10u
typedef void (*sand_thread_entry)(void *);
/* Called from the scheduler (interrupts off) whenever a different thread is
 * about to run, so the process layer can switch address space and TSS stack. */
typedef void (*sand_switch_hook)(uint32_t thread_id);
void scheduler_set_switch_hook(sand_switch_hook hook);
/* Variants for callers already inside an interrupt/syscall frame. They take
 * the saved stack pointer and return the stack pointer to resume. */
uint32_t scheduler_exit_current(uint32_t saved_stack);
uint32_t scheduler_block_current_irq(const void *wait_object, uint32_t saved_stack);
int scheduler_wake_all(const void *wait_object);
int scheduler_init(void);
int scheduler_create(sand_thread_entry entry, void *argument);
uint32_t scheduler_reschedule(uint32_t saved_stack);
void scheduler_yield(void);
void scheduler_block_current(const void *wait_object);
int scheduler_wake_one(const void *wait_object);
uint32_t scheduler_current_id(void);
uint32_t scheduler_switch_count(void);
uint32_t scheduler_thread_count(void);
void scheduler_idle(void);
void scheduler_thread_bootstrap(void);
void scheduler_thread_exit(void);
#endif
