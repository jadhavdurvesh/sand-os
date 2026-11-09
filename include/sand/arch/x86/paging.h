#ifndef SAND_ARCH_X86_PAGING_H
#define SAND_ARCH_X86_PAGING_H
#include <stdint.h>
int paging_init(void);
int paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
int paging_is_enabled(void);
#endif
