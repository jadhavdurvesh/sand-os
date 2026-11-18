#include "sand/libsand.h"
/* netecho: waits up to 30 s for one UDP datagram on port 7777 and sends it back to its sender. */
void _start(void)
{
    static struct sand_udp d;
    sand_puts("netecho: listening on udp/7777\n");
    for (int i = 0; i < 600; ++i) {
        int32_t rc = sand_net_udp_recv(7777, &d);
        if (rc == 1) {
            uint32_t peer = d.ip; uint16_t pport = d.src_port;
            d.ip = peer; d.dst_port = pport; d.src_port = 7777;
            if (sand_net_udp_send(&d) == 0) { sand_puts("netecho: echoed "); sand_putu(d.length); sand_puts(" bytes\n"); sand_exit(0); }
            sand_puts("netecho: send failed\n"); sand_exit(1);
        }
        if (rc < 0) { sand_puts("netecho: no network ("); sand_putu((uint32_t)-rc); sand_puts(")\n"); sand_exit(2); }
        sand_sleep_ms(50);
    }
    sand_puts("netecho: timed out\n"); sand_exit(3);
}
