#include "sand/libsand.h"
/* HERALD: SAND's background service. No window. Other apps talk to it over TIDEWAY:
 *   channel "herald.req":  "R<seconds> <text>"  schedule a notification (text <= 60 chars)
 *                          "C"                   cancel everything
 *                          "Q"                   list what is pending on "herald.rep"
 *   channel "herald.rep":  "L<seconds left> <text>" per pending item, then "E"
 *   channel "herald.notify": what the desktop shows as a toast (the kernel stamps who sent it). */
#define MAX 8
static struct { uint32_t due; char text[61]; } item[MAX];
static uint32_t n;
static uint32_t hz = 100;
static int32_t req, rep, note;

static void notify(const char *t)
{
    uint32_t len = 0; while (t[len] && len < 60) ++len;
    if (len == 0) { t = "time is up"; len = 10; }
    (void)sand_chan_send(note, t, len);
}
static void handle(const struct sand_chan_msg *m)
{
    const char *d = (const char *)m->data; uint32_t len = m->length;
    if (len == 0) return;
    if (d[0] == 'C') { n = 0; return; }
    if (d[0] == 'Q') {
        for (uint32_t i = 0; i < n; ++i) {
            char out[72]; uint32_t o = 0, tl = 0;
            int32_t dt = (int32_t)(item[i].due - sand_ticks()); uint32_t left = dt > 0 ? ((uint32_t)dt + hz - 1u) / hz : 0u;
            out[o++] = 'L'; char dig[10]; int k = 0; do { dig[k++] = (char)('0' + left % 10u); left /= 10u; } while (left);
            while (k) out[o++] = dig[--k];
            out[o++] = ' '; while (item[i].text[tl]) out[o++] = item[i].text[tl++];
            (void)sand_chan_send(rep, out, o);
        }
        (void)sand_chan_send(rep, "E", 1);
        return;
    }
    if (d[0] == 'R' && n < MAX) {
        uint32_t secs = 0, i = 1;
        while (i < len && d[i] >= '0' && d[i] <= '9' && secs < 100000u) secs = secs * 10u + (uint32_t)(d[i++] - '0');
        if (secs == 0 || secs > 86400u) return;
        if (i < len && d[i] == ' ') ++i;
        uint32_t tl = 0; while (i < len && tl < 60) item[n].text[tl++] = d[i++];
        item[n].text[tl] = 0; item[n].due = sand_ticks() + secs * hz; ++n;
    }
}
void _start(void)
{
    struct sand_sysinfo si; if (sand_sysinfo(&si) == 0 && si.tick_hz) hz = si.tick_hz;
    req = sand_chan_open("herald.req"); rep = sand_chan_open("herald.rep"); note = sand_chan_open("herald.notify");
    if (req < 1 || rep < 1 || note < 1) sand_exit(1);
    for (;;) {
        struct sand_chan_msg m;
        while (sand_chan_recv(req, &m) == 1) handle(&m);
        uint32_t now = sand_ticks();
        for (uint32_t i = 0; i < n; ) {
            if ((int32_t)(now - item[i].due) >= 0) { notify(item[i].text); for (uint32_t k = i + 1; k < n; ++k) item[k - 1] = item[k]; --n; }
            else ++i;
        }
        sand_sleep_ms(50);
    }
}
