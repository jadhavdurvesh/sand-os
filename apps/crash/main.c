#include "sand/libsand.h"
/* Deliberately dereferences kernel memory from ring 3. The kernel must kill
 * only this process (status -256 - 14) and keep running. */
void _start(void)
{
    sand_puts("crash: about to fault\n");
    volatile uint32_t *kernel_memory = (volatile uint32_t *)0x00010000u;
    *kernel_memory = 0xDEADBEEFu;
    sand_puts("crash: SURVIVED (bug)\n");
    sand_exit(0);
}
