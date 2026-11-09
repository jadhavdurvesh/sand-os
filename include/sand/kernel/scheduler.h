#ifndef SAND_KERNEL_SCHEDULER_H
#define SAND_KERNEL_SCHEDULER_H
#include <stdint.h>
#define SAND_SCHEDULER_MAX_THREADS 4u
typedef void (*sand_thread_entry)(void *);
int scheduler_init(void);
int scheduler_create(sand_thread_entry entry, void *argument);
uint32_t scheduler_tick(uint32_t saved_stack);
uint32_t scheduler_switch_count(void);
uint32_t scheduler_thread_count(void);
void scheduler_thread_bootstrap(void);
void scheduler_thread_exit(void);
#endif
