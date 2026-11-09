#include "sand/kernel/scheduler.h"
#define THREAD_STACK_SIZE 4096u
#define KERNEL_CS 0x08u
#define INITIAL_EFLAGS 0x202u
enum thread_state { THREAD_UNUSED = 0, THREAD_RUNNABLE, THREAD_DEAD };
struct sand_thread {
    uint32_t *saved_stack;
    sand_thread_entry entry;
    void *argument;
    enum thread_state state;
};
static struct sand_thread threads[SAND_SCHEDULER_MAX_THREADS];
static uint8_t stacks[SAND_SCHEDULER_MAX_THREADS][THREAD_STACK_SIZE] __attribute__((aligned(16)));
static uint32_t current_thread;
static uint32_t thread_count;
static uint32_t switches;
static uint32_t scheduler_ready;
static uint32_t interrupts_enabled;
extern void x86_thread_bootstrap(void);
static uint32_t *make_initial_stack(uint32_t id) {
    uint32_t *sp = (uint32_t *)(stacks[id] + THREAD_STACK_SIZE);
    *--sp = INITIAL_EFLAGS;
    *--sp = KERNEL_CS;
    *--sp = (uint32_t)x86_thread_bootstrap;
    /* popal layout: EDI, ESI, EBP, ignored ESP, EBX, EDX, ECX, EAX */
    *--sp = 0u; /* EAX */
    *--sp = 0u; /* ECX */
    *--sp = 0u; /* EDX */
    *--sp = 0u; /* EBX */
    *--sp = 0u; /* ignored ESP */
    *--sp = 0u; /* EBP */
    *--sp = 0u; /* ESI */
    *--sp = 0u; /* EDI */
    return sp;
}
int scheduler_init(void) {
    for (uint32_t i = 0; i < SAND_SCHEDULER_MAX_THREADS; ++i) {
        threads[i].saved_stack = 0;
        threads[i].entry = 0;
        threads[i].argument = 0;
        threads[i].state = THREAD_UNUSED;
    }
    current_thread = 0u;
    thread_count = 1u; /* Boot context is thread zero. */
    switches = 0u;
    scheduler_ready = 1u;
    interrupts_enabled = 0u;
    threads[0].state = THREAD_RUNNABLE;
    return 0;
}
int scheduler_create(sand_thread_entry entry, void *argument) {
    if (!scheduler_ready || entry == 0 || thread_count >= SAND_SCHEDULER_MAX_THREADS) return -1;
    uint32_t id = thread_count++;
    threads[id].entry = entry;
    threads[id].argument = argument;
    threads[id].saved_stack = make_initial_stack(id);
    threads[id].state = THREAD_RUNNABLE;
    return (int)id;
}
uint32_t scheduler_tick(uint32_t saved_stack) {
    if (!scheduler_ready) return saved_stack;
    threads[current_thread].saved_stack = (uint32_t *)saved_stack;
    uint32_t candidate = current_thread;
    for (uint32_t step = 1u; step <= thread_count; ++step) {
        uint32_t index = (current_thread + step) % thread_count;
        if (threads[index].state == THREAD_RUNNABLE && threads[index].saved_stack != 0) {
            candidate = index;
            break;
        }
    }
    if (candidate != current_thread) {
        current_thread = candidate;
        ++switches;
    }
    return (uint32_t)threads[current_thread].saved_stack;
}
static sand_thread_entry current_entry(void) { return threads[current_thread].entry; }
static void *current_argument(void) { return threads[current_thread].argument; }
void scheduler_thread_bootstrap(void) {
    sand_thread_entry entry = current_entry();
    void *argument = current_argument();
    if (entry != 0) entry(argument);
    scheduler_thread_exit();
}
void scheduler_thread_exit(void) {
    __asm__ volatile ("cli");
    threads[current_thread].state = THREAD_DEAD;
    for (;;) __asm__ volatile ("sti; hlt");
}
uint32_t scheduler_switch_count(void) { return switches; }
uint32_t scheduler_thread_count(void) { return thread_count; }
