#include "sand/kernel/user.h"
#include "sand/arch/x86/paging.h"
#include "sand/mm/frame.h"
#define USER_CODE_BASE 0x40000000u
#define USER_STACK_TOP 0xBFFFE000u
#define USER_PAGE_FLAGS 0x007u
extern uint8_t __user_text_start[];
extern uint8_t __user_text_end[];
extern void x86_enter_user(uint32_t entry,uint32_t stack_top);
static uint32_t user_stack_physical;
int user_space_init(void) {
    uint32_t code_physical=(uint32_t)__user_text_start & 0xFFFFF000u;
    uint32_t code_end=((uint32_t)__user_text_end+0xFFFu)&0xFFFFF000u;
    if(code_end-code_physical!=SAND_PAGE_SIZE) return -1;
    user_stack_physical=frame_alloc();
    if(user_stack_physical==0u) return -1;
    if(paging_map_page(USER_CODE_BASE,code_physical,USER_PAGE_FLAGS)!=0) return -1;
    if(paging_map_page(USER_STACK_TOP-SAND_PAGE_SIZE,user_stack_physical,USER_PAGE_FLAGS|0x002u)!=0) return -1;
    return 0;
}
void user_mode_enter(uint32_t entry,uint32_t stack_top) {
    tss_set_kernel_stack(0x90000u);
    x86_enter_user(entry,stack_top);
}
