#ifndef SAND_ARCH_X86_PAGING_H
#define SAND_ARCH_X86_PAGING_H
#include <stdint.h>
int paging_init(void);
int paging_map_page(uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
int paging_space_create(uint32_t *directory_physical);
int paging_space_map_page(uint32_t directory_physical, uint32_t virtual_address, uint32_t physical_address, uint32_t flags);
int paging_space_destroy(uint32_t directory_physical);
int paging_activate(uint32_t directory_physical);
uint32_t paging_kernel_directory(void);
uint32_t paging_current_directory(void);
int paging_is_enabled(void);
/* Validate that [address, address+length) lies in the user range and every
 * page is present, user-accessible (and writable when requested) in the
 * given directory. Used by the syscall layer before touching user memory. */
int paging_user_range_ok(uint32_t directory_physical, uint32_t address, uint32_t length, int writable);
/* Make sure the page table covering a kernel virtual address exists now, so
 * every address space created later inherits it. */
int paging_reserve_table(uint32_t virtual_address);
#endif
