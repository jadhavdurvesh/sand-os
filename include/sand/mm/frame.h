#ifndef SAND_MM_FRAME_H
#define SAND_MM_FRAME_H
#include <stdint.h>
#define SAND_PAGE_SIZE 4096u
#define SAND_FRAME_LIMIT 4096u /* Initial allocator manages the first 16 MiB. */
int frame_allocator_init(void);
uint32_t frame_alloc(void);
int frame_free(uint32_t physical_address);
uint32_t frame_free_count(void);
#endif
