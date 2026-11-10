#include <sand/app.h>

void _start(void)
{
    static const char message[] = "Hello from a native SAND application!\n";
    (void)sand_write(message, (uint32_t)(sizeof(message) - 1u));
    sand_exit(0);
}
