#include "sand/kernel/process.h"
#include "sand/arch/x86/paging.h"
#include "sand/arch/x86/gdt.h"
#include "sand/mm/frame.h"
#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0xBFFFE000u
#define USER_CODE_FLAGS 0x005u
#define USER_STACK_FLAGS 0x007u
struct process_slot { struct sand_process_info info; uint32_t code_frame; uint32_t stack_frame; };
static struct process_slot slots[SAND_PROCESS_LIMIT];
static sand_pid_t active_pid;
static sand_pid_t next_pid=1u;
static uint8_t process_kernel_stack[8192] __attribute__((aligned(16)));
extern uint8_t __user_text_start[];
extern uint8_t __user_text_end[];
extern void x86_enter_user(uint32_t entry,uint32_t stack_top);
int process_system_init(void) {
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) { slots[i].info.pid=0u; slots[i].info.state=SAND_PROCESS_UNUSED; slots[i].info.page_directory=0u; slots[i].code_frame=0u; slots[i].stack_frame=0u; }
    active_pid=0u; next_pid=1u; return 0;
}
static struct process_slot *find_slot(sand_pid_t pid) {
    if(pid==0u) return 0;
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) if(slots[i].info.pid==pid && slots[i].info.state!=SAND_PROCESS_UNUSED) return &slots[i];
    return 0;
}
int process_create_test(const char *name,sand_pid_t *pid_out) {
    if(name==0 || pid_out==0) return -1;
    uint32_t code_size=(uint32_t)(__user_text_end-__user_text_start);
    if(code_size==0u || code_size>SAND_PAGE_SIZE) return -1;
    struct process_slot *slot=0;
    for(uint32_t i=0;i<SAND_PROCESS_LIMIT;++i) if(slots[i].info.state==SAND_PROCESS_UNUSED) { slot=&slots[i]; break; }
    if(slot==0) return -1;
    uint32_t directory=0u,code=frame_alloc(),stack=frame_alloc();
    if(code==0u || stack==0u) { if(code) frame_free(code); if(stack) frame_free(stack); return -1; }
    if(paging_space_create(&directory)!=0) { frame_free(code); frame_free(stack); return -1; }
    uint8_t *dst=(uint8_t *)code;
    for(uint32_t i=0;i<SAND_PAGE_SIZE;++i) dst[i]=0u;
    for(uint32_t i=0;i<code_size;++i) dst[i]=__user_text_start[i];
    if(paging_space_map_page(directory,USER_CODE_BASE,code,USER_CODE_FLAGS)!=0) { paging_space_destroy(directory); frame_free(code); frame_free(stack); return -1; }
    if(paging_space_map_page(directory,USER_STACK_TOP-SAND_PAGE_SIZE,stack,USER_STACK_FLAGS)!=0) { paging_space_destroy(directory); frame_free(stack); return -1; }
    sand_pid_t pid=next_pid++; if(next_pid==0u) next_pid=1u;
    slot->info.pid=pid; slot->info.state=SAND_PROCESS_READY; slot->info.page_directory=directory;
    slot->info.entry=USER_CODE_BASE; slot->info.user_stack=USER_STACK_TOP;
    uint32_t n=0u; while(name[n] && n<SAND_PROCESS_NAME_MAX-1u) { slot->info.name[n]=name[n]; ++n; }
    slot->info.name[n]='\0'; slot->code_frame=code; slot->stack_frame=stack; *pid_out=pid; return 0;
}
int process_activate(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_TERMINATED) return -1;
    if(active_pid!=0u) { struct process_slot *old=find_slot(active_pid); if(old && old->info.state==SAND_PROCESS_RUNNING) old->info.state=SAND_PROCESS_READY; }
    if(paging_activate(slot->info.page_directory)!=0) return -1;
    slot->info.state=SAND_PROCESS_RUNNING; active_pid=pid; return 0;
}
int process_terminate(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_TERMINATED) return -1;
    if(active_pid==pid) { if(paging_activate(paging_kernel_directory())!=0) return -1; active_pid=0u; }
    slot->info.state=SAND_PROCESS_TERMINATED; return 0;
}
int process_destroy(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || slot->info.state==SAND_PROCESS_RUNNING || active_pid==pid) return -1;
    if(slot->info.state!=SAND_PROCESS_TERMINATED) slot->info.state=SAND_PROCESS_TERMINATED;
    if(paging_space_destroy(slot->info.page_directory)!=0) return -1;
    /* paging_space_destroy reclaimed the user-mapped code and stack frames. */
    slot->code_frame=0u; slot->stack_frame=0u; slot->info.page_directory=0u;
    slot->info.state=SAND_PROCESS_UNUSED; slot->info.pid=0u; return 0;
}
int process_get_info(sand_pid_t pid,struct sand_process_info *out) { struct process_slot *slot=find_slot(pid); if(slot==0 || out==0) return -1; *out=slot->info; return 0; }
sand_pid_t process_current(void) { return active_pid; }
void process_launch(sand_pid_t pid) {
    struct process_slot *slot=find_slot(pid); if(slot==0 || process_activate(pid)!=0) for(;;) __asm__ volatile("cli; hlt");
    tss_set_kernel_stack((uint32_t)(process_kernel_stack+sizeof(process_kernel_stack))); x86_enter_user(slot->info.entry,slot->info.user_stack);
    for(;;) __asm__ volatile("cli; hlt");
}
