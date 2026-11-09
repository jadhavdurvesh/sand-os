#ifndef SAND_MM_HEAP_H
#define SAND_MM_HEAP_H
#include <stdint.h>
int kheap_init(void);
void *kmalloc(uint32_t size);
uint32_t kheap_used(void);
#endif
