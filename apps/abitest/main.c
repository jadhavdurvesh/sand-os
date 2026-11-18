#include "sand/libsand.h"
/* Exercises the syscall ABI from ring 3. Exit status = failed checks. */
static uint32_t failures;
static void check(int ok, const char *what)
{
    if (!ok) { ++failures; sand_puts("ABITEST FAIL: "); sand_puts(what); sand_puts("\n"); }
}
void _start(void)
{
    check(sand_getpid() > 0, "getpid");
    uint32_t t0 = sand_ticks();
    sand_sleep_ms(50);
    check(sand_ticks() - t0 >= 5u, "sleep advances ticks");
    sand_yield();

    /* The kernel must reject pointers instead of dereferencing them. */
    check(sand_write((const char *)0x00010000u, 4) == SAND_E_FAULT, "kernel pointer rejected");
    check(sand_write((const char *)0xE0000000u, 4) == SAND_E_FAULT, "mmio pointer rejected");
    check(sand_write((const char *)0, 4) == SAND_E_FAULT, "null pointer rejected");
    check(sand_syscall0(9999u) == SAND_E_NOSYS, "unknown syscall");

    static const char payload[] = "abi-roundtrip-payload";
    char back[64];
    check(sand_fs_write("abitest.dat", payload, sizeof(payload)) == 0, "fs_write");
    memset(back, 0, sizeof(back));
    check(sand_fs_read("abitest.dat", back, sizeof(back)) == (int32_t)sizeof(payload), "fs_read length");
    check(memcmp(back, payload, sizeof(payload)) == 0, "fs_read bytes");
    uint32_t seen = 0;
    struct sand_dirent entry;
    for (uint32_t i = 0; sand_fs_list(i, &entry) == 0; ++i)
        if (strcmp(entry.name, "abitest.dat") == 0 && entry.length == sizeof(payload)) ++seen;
    check(seen == 1u, "fs_list finds file");
    check(sand_fs_remove("abitest.dat") == 0, "fs_remove");
    check(sand_fs_read("abitest.dat", back, sizeof(back)) == SAND_E_NOENT, "removed file is gone");

    /* BEDROCK: whole files well beyond the old 512-byte limit round-trip intact */
    static uint8_t big[20000], bigback[20000];
    for (uint32_t i = 0; i < sizeof(big); ++i) big[i] = (uint8_t)(i * 7u + (i >> 8));
    check(sand_fs_write("abibig.dat", big, sizeof(big)) == 0, "fs_write 20000 bytes");
    check(sand_fs_read("abibig.dat", bigback, sizeof(bigback)) == (int32_t)sizeof(big), "fs_read 20000 length");
    check(memcmp(big, bigback, sizeof(big)) == 0, "fs_read 20000 bytes");
    check(sand_fs_remove("abibig.dat") == 0, "fs_remove big");

    /* TIDEWAY: loopback through a named channel; kernel stamps the sender pid */
    int32_t ch = sand_chan_open("abitest.loop");
    check(ch >= 1, "chan_open");
    check(sand_chan_open("abitest.loop") == ch, "chan_open is idempotent by name");
    check(sand_chan_open("Bad Name") == SAND_E_FAIL, "chan_open rejects bad names");
    struct sand_chan_msg m;
    check(sand_chan_recv(ch, &m) == 0, "empty channel gives 0");
    check(sand_chan_send(ch, "one", 3) == 0 && sand_chan_send(ch, "two!", 4) == 0, "chan_send");
    check(sand_chan_send(ch, "x", 0) == SAND_E_FAIL, "empty message refused");
    check(sand_chan_recv(ch, &m) == 1 && m.length == 3 && memcmp(m.data, "one", 3) == 0 && m.from == (uint32_t)sand_getpid(), "chan_recv first, sender stamped");
    check(sand_chan_recv(ch, &m) == 1 && m.length == 4 && memcmp(m.data, "two!", 4) == 0, "chan_recv second in order");
    check(sand_chan_recv(ch, &m) == 0, "drained");
    int sent = 0; while (sand_chan_send(ch, "f", 1) == 0) ++sent;
    check(sent == 8, "channel holds exactly 8 messages");
    check(sand_chan_send(ch, "f", 1) == SAND_E_FULL, "full channel reports SAND_E_FULL");
    while (sand_chan_recv(ch, &m) == 1) {}

    struct sand_sysinfo info;
    check(sand_sysinfo(&info) == 0 && info.abi_version == SAND_ABI_VERSION && info.tick_hz == 100u, "sysinfo");

    int32_t child = sand_spawn("hello");
    check(child > 0, "spawn");
    int32_t status = -1;
    check(sand_wait(child, &status) == 0 && status == 0, "wait child status");
    check(sand_wait(child, &status) == SAND_E_NOENT, "child reaped once");
    check(sand_spawn("no-such-app") == SAND_E_NOENT, "spawn unknown app");

    sand_puts(failures == 0 ? "ABITEST:PASS\n" : "ABITEST:FAILED\n");
    sand_exit((int32_t)failures);
}
