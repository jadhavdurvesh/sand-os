#include "sand/libsand.h"
/* CPU-bound worker: proves two apps make progress concurrently under the
 * preemptive scheduler. Exit status = iterations / 1,000,000 (always 5). */
void _start(void)
{
    volatile uint32_t sink = 0;
    for (uint32_t i = 0; i < 5000000u; ++i) sink += i;
    sand_puts("spin pid ");
    sand_putu((uint32_t)sand_getpid());
    sand_puts(" done\n");
    sand_exit(5);
}
