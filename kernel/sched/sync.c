#include "sand/kernel/sync.h"
#include "sand/kernel/scheduler.h"
static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint32_t flags) {
    if((flags&0x200u)!=0u) __asm__ volatile ("sti" : : : "memory");
}
void semaphore_init(sand_semaphore_t *semaphore,uint32_t initial_count) {
    if(semaphore!=0) semaphore->count=initial_count;
}
void semaphore_wait(sand_semaphore_t *semaphore) {
    if(semaphore==0) return;
    for(;;) {
        uint32_t flags=irq_save();
        if(semaphore->count!=0u) {
            --semaphore->count; irq_restore(flags); return;
        }
        scheduler_block_current(semaphore);
        irq_restore(flags);
    }
}
void semaphore_post(sand_semaphore_t *semaphore) {
    if(semaphore==0) return;
    uint32_t flags=irq_save();
    if(semaphore->count!=0xFFFFFFFFu) ++semaphore->count;
    (void)scheduler_wake_one(semaphore);
    irq_restore(flags);
}
void mutex_init(sand_mutex_t *mutex) {
    if(mutex!=0) semaphore_init(&mutex->semaphore,1u);
}
void mutex_lock(sand_mutex_t *mutex) {
    if(mutex!=0) semaphore_wait(&mutex->semaphore);
}
void mutex_unlock(sand_mutex_t *mutex) {
    if(mutex!=0) semaphore_post(&mutex->semaphore);
}
