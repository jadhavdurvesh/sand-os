#include "sand/kernel/scheduler.h"
#include "sand/arch/x86/idt.h"
#define THREAD_STACK_SIZE 4096u
#define KERNEL_CS 0x08u
#define INITIAL_EFLAGS 0x202u
enum thread_state { THREAD_UNUSED=0, THREAD_RUNNABLE, THREAD_BLOCKED, THREAD_DEAD, THREAD_IDLE };
struct sand_thread {
    uint32_t *saved_stack;
    sand_thread_entry entry;
    void *argument;
    const void *wait_object;
    enum thread_state state;
    uint32_t stack_slot;
};
static struct sand_thread threads[SAND_SCHEDULER_MAX_THREADS];
static uint8_t stack_pool[SAND_SCHEDULER_MAX_THREADS][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t stack_used[SAND_SCHEDULER_MAX_THREADS];
static uint32_t current_thread, switches, scheduler_ready;
extern void x86_thread_bootstrap(void);
extern void x86_reschedule(void);

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}
static void irq_restore(uint32_t flags) {
    if ((flags & 0x200u) != 0u) __asm__ volatile ("sti" : : : "memory");
}
static int stack_alloc(void) {
    for(uint32_t i=0;i<SAND_SCHEDULER_MAX_THREADS;++i) {
        if(!stack_used[i]) { stack_used[i]=1u; return (int)i; }
    }
    return -1;
}
static void stack_release(uint32_t slot) {
    if(slot<SAND_SCHEDULER_MAX_THREADS) stack_used[slot]=0u;
}
static uint32_t *make_initial_stack(uint32_t slot) {
    uint32_t *sp=(uint32_t *)(stack_pool[slot]+THREAD_STACK_SIZE);
    *--sp=INITIAL_EFLAGS; *--sp=KERNEL_CS; *--sp=(uint32_t)x86_thread_bootstrap;
    *--sp=0u; *--sp=0u; *--sp=0u; *--sp=0u;
    *--sp=0u; *--sp=0u; *--sp=0u; *--sp=0u;
    return sp;
}
int scheduler_init(void) {
    uint32_t flags=irq_save();
    for(uint32_t i=0;i<SAND_SCHEDULER_MAX_THREADS;++i) {
        threads[i].saved_stack=0; threads[i].entry=0; threads[i].argument=0;
        threads[i].wait_object=0; threads[i].state=THREAD_UNUSED;
        threads[i].stack_slot=0u; stack_used[i]=0u;
    }
    current_thread=0u; switches=0u; scheduler_ready=1u;
    idt_set_reschedule(x86_reschedule); idt_load();
    threads[0].state=THREAD_IDLE;
    irq_restore(flags);
    return 0;
}
int scheduler_create(sand_thread_entry entry, void *argument) {
    if(entry==0 || !scheduler_ready) return -1;
    uint32_t flags=irq_save();
    for(uint32_t id=1u;id<SAND_SCHEDULER_MAX_THREADS;++id) {
        if(threads[id].state==THREAD_UNUSED || threads[id].state==THREAD_DEAD) {
            if(threads[id].state==THREAD_DEAD) stack_release(threads[id].stack_slot);\n            int slot=stack_alloc();
            if(slot<0) { irq_restore(flags); return -1; }
            threads[id].entry=entry; threads[id].argument=argument;
            threads[id].wait_object=0; threads[id].stack_slot=(uint32_t)slot;
            threads[id].saved_stack=make_initial_stack((uint32_t)slot);
            threads[id].state=THREAD_RUNNABLE;
            irq_restore(flags);
            return (int)id;
        }
    }
    irq_restore(flags);
    return -1;
}
uint32_t scheduler_reschedule(uint32_t saved_stack) {
    if(!scheduler_ready) return saved_stack;
    if(threads[current_thread].state!=THREAD_DEAD)
        threads[current_thread].saved_stack=(uint32_t *)saved_stack;
    uint32_t candidate=0u;
    for(uint32_t step=1u;step<=SAND_SCHEDULER_MAX_THREADS;++step) {
        uint32_t index=(current_thread+step)%SAND_SCHEDULER_MAX_THREADS;
        if(threads[index].state==THREAD_RUNNABLE && threads[index].saved_stack!=0) {
            candidate=index; break;
        }
    }
    if(candidate==0u && threads[0].state!=THREAD_IDLE &&
       threads[0].state!=THREAD_RUNNABLE) return saved_stack;
    if(candidate!=current_thread) { current_thread=candidate; ++switches; }
    return (uint32_t)threads[current_thread].saved_stack;
}
void scheduler_yield(void) { __asm__ volatile ("int $0x30" : : : "memory"); }
void scheduler_block_current(const void *wait_object) {
    uint32_t flags=irq_save();
    threads[current_thread].wait_object=wait_object;
    threads[current_thread].state=THREAD_BLOCKED;
    __asm__ volatile ("int $0x30" : : : "memory");
    irq_restore(flags);
}
int scheduler_wake_one(const void *wait_object) {
    if(wait_object==0) return -1;
    uint32_t flags=irq_save();
    for(uint32_t i=1u;i<SAND_SCHEDULER_MAX_THREADS;++i) {
        if(threads[i].state==THREAD_BLOCKED && threads[i].wait_object==wait_object) {
            threads[i].wait_object=0; threads[i].state=THREAD_RUNNABLE;
            irq_restore(flags); return (int)i;
        }
    }
    irq_restore(flags); return -1;
}
uint32_t scheduler_current_id(void) { return current_thread; }
static sand_thread_entry current_entry(void) { return threads[current_thread].entry; }
static void *current_argument(void) { return threads[current_thread].argument; }
void scheduler_thread_bootstrap(void) {
    sand_thread_entry entry=current_entry(); void *argument=current_argument();
    if(entry!=0) entry(argument);
    scheduler_thread_exit();
}
void scheduler_thread_exit(void) {
    uint32_t flags=irq_save(); (void)flags;
    threads[current_thread].state=THREAD_DEAD;
    threads[current_thread].wait_object=0;
    scheduler_yield();
    for(;;) __asm__ volatile ("cli; hlt");
}
void scheduler_idle(void) { for(;;) __asm__ volatile ("sti; hlt" : : : "memory"); }
uint32_t scheduler_switch_count(void) { return switches; }
uint32_t scheduler_thread_count(void) {
    uint32_t count=0;
    for(uint32_t i=1u;i<SAND_SCHEDULER_MAX_THREADS;++i)
        if(threads[i].state==THREAD_RUNNABLE || threads[i].state==THREAD_BLOCKED) ++count;
    return count;
}
