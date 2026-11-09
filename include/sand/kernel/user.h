#ifndef SAND_KERNEL_USER_H
#define SAND_KERNEL_USER_H
#include <stdint.h>
int user_space_init(void);
void user_mode_enter(uint32_t entry, uint32_t stack_top);
#endif
