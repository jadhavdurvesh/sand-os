#include "sand/kernel/user.h"
#include "sand/arch/x86/gdt.h"
#include "sand/arch/x86/paging.h"
#include "sand/mm/frame.h"
#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0xBFFFE000u
#define PAGE_USER_READONLY 0x005u
#define PAGE_USER_WRITE 0x007u
extern uint8_t __user_text_start[];
extern uint8_t __user_text_end[];
extern void x86_enter_user(uint32_t entry,uint32_t stack_top);
static uint32_t user_entry_address;
int user_space_init(void) {
    uint32_t code_size=(uint32_t)(__user_text_end-__user_text_start);
    if(code_size==0u || code_size>SAND_PAGE_SIZE) return -1;
    uint32_t code_physical=frame_alloc();
    uint32_t stack_physical=frame_alloc();
    if(code_physical==0u || stack_physical==0u) return -1;
    uint8_t *destination=(uint8_t *)code_physical;
    for(uint32_t i=0;i<SAND_PAGE_SIZE;++i) destination[i]=0u;
    for(uint32_t i=0;i<code_size;++i) destination[i]=__user_text_start[i];
    if(paging_map_page(USER_CODE_BASE,code_physical,PAGE_USER_READONLY)!=0) return -1;
    if(paging_map_page(USER_STACK_TOP-SAND_PAGE_SIZE,stack_physical,PAGE_USER_WRITE)!=0) return -1;
    user_entry_address=USER_CODE_BASE;
    return 0;
}
void user_mode_enter(uint32_t entry,uint32_t stack_top) {
    tss_set_kernel_stack(0x90000u);
    x86_enter_user(entry?entry:user_entry_address,stack_top?stack_top:USER_STACK_TOP);
}
