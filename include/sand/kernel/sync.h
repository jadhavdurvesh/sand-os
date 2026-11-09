#ifndef SAND_KERNEL_SYNC_H
#define SAND_KERNEL_SYNC_H
#include <stdint.h>
typedef struct { volatile uint32_t count; } sand_semaphore_t;
typedef struct { sand_semaphore_t semaphore; } sand_mutex_t;
void semaphore_init(sand_semaphore_t *semaphore, uint32_t initial_count);
void semaphore_wait(sand_semaphore_t *semaphore);
void semaphore_post(sand_semaphore_t *semaphore);
void mutex_init(sand_mutex_t *mutex);
void mutex_lock(sand_mutex_t *mutex);
void mutex_unlock(sand_mutex_t *mutex);
#endif
