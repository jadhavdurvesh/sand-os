#include "sand/libsand.h"
void _start(void)
{
    sand_puts("Hello from a native SAND application (pid ");
    sand_putu((uint32_t)sand_getpid());
    sand_puts(")\n");
    sand_exit(0);
}
