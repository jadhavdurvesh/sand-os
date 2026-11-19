#include "sand/libsand.h"
/* netget: one TCP round trip with the host behind the gateway (QEMU's user network maps the gateway to the host). */
void _start(void)
{
    struct sand_netinfo ni; static char buf[256];
    if (sand_net_info(&ni) != 0) { sand_puts("netget: no network\n"); sand_exit(2); }
    uint32_t gw = (uint32_t)ni.gateway[0] | ((uint32_t)ni.gateway[1] << 8) | ((uint32_t)ni.gateway[2] << 16) | ((uint32_t)ni.gateway[3] << 24);
    int32_t rc = sand_tcp_connect(gw, 7778, 5000);
    if (rc != 0) { sand_puts("netget: connect failed ("); sand_putu((uint32_t)-rc); sand_puts(")\n"); sand_exit(1); }
    sand_puts("netget: connected\n");
    if (sand_tcp_send("hello tcp\n", 10) != 10) { sand_puts("netget: send failed\n"); sand_exit(1); }
    uint32_t got = 0;
    for (int i = 0; i < 100 && got < sizeof(buf) - 1; ++i) {
        int32_t n = sand_tcp_recv(buf + got, (uint32_t)(sizeof(buf) - 1 - got));
        if (n > 0) { got += (uint32_t)n; if (buf[got - 1] == '\n') break; }
        else if (n < 0) break;
        else sand_sleep_ms(50);
    }
    buf[got] = 0;
    sand_puts("netget: reply: "); sand_puts(buf);
    sand_tcp_close();
    sand_puts("netget: closed\n");
    sand_exit(0);
}
