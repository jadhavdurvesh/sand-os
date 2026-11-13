/* sandsh - the SAND native shell. Runs in ring 3 and talks to the kernel only
 * through the documented syscall ABI (sdk/sapp/include/sand/abi.h). */
#include "sand/libsand.h"

#define LINE_MAX 200u
#define ARG_MAX 8u

/* Palette from the SAND design system, as truecolor SGR sequences. */
#define C_RESET  "\x1b[0m"
#define C_ACCENT "\x1b[38;2;169;155;255m"
#define C_MUTED  "\x1b[38;2;146;153;173m"
#define C_GOOD   "\x1b[38;2;130;200;173m"
#define C_WARN   "\x1b[38;2;215;189;141m"
#define C_BAD    "\x1b[38;2;217;137;145m"

static void out(const char *s) { sand_puts(s); }
static void pad(uint32_t used, uint32_t width) { while (used++ < width) out(" "); }
static void line_of(const char *label, uint32_t value, const char *unit)
{
    out(C_MUTED); out(label); pad(strlen(label), 18); out(C_RESET);
    sand_putu(value); out(unit); out("\n");
}

static uint32_t split(char *line, char *argv[], uint32_t max)
{
    uint32_t argc = 0;
    char *p = line;
    while (*p && argc < max) {
        while (*p == ' ') *p++ = '\0';
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') ++p;
    }
    return argc;
}

static void cmd_help(void)
{
    out(C_ACCENT "SAND shell" C_RESET C_MUTED "  -  commands" C_RESET "\n");
    static const char *const rows[][2] = {
        {"help", "show this list"},
        {"version", "kernel / ABI / shell versions"},
        {"echo <text>", "print text"},
        {"clear", "clear the screen"},
        {"ls", "list files on SANDFS"},
        {"cat <file>", "print a file"},
        {"write <file> <text>", "create or replace a file"},
        {"rm <file>", "remove a file"},
        {"apps", "list installed applications"},
        {"run <app>", "launch an app and wait for it"},
        {"ps", "list processes"},
        {"mem", "memory and scheduler counters"},
        {"uptime", "time since boot"},
        {"devices", "list registered devices"},
        {"sleep <ms>", "wait"},
        {"exit", "end this shell session"},
    };
    for (uint32_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        out("  " C_ACCENT); out(rows[i][0]); pad(strlen(rows[i][0]), 22); out(C_RESET C_MUTED);
        out(rows[i][1]); out(C_RESET "\n");
    }
}

static void cmd_version(void)
{
    struct sand_sysinfo si;
    out("SAND OS  "); out(C_MUTED "x86-32 native kernel" C_RESET "\n");
    if (sand_sysinfo(&si) == 0) { out("syscall ABI v"); sand_putu(si.abi_version); out("\n"); }
}

static void cmd_ls(void)
{
    struct sand_dirent e;
    uint32_t n = 0;
    for (uint32_t i = 0; sand_fs_list(i, &e) == 0; ++i) {
        out("  " C_ACCENT); out(e.name); pad(strlen(e.name), 26); out(C_RESET);
        sand_putu(e.length); out(" bytes\n"); ++n;
    }
    if (n == 0) out(C_MUTED "  (no files)" C_RESET "\n");
}

static void cmd_cat(const char *name)
{
    static char buf[512];
    int32_t n = sand_fs_read(name, buf, sizeof(buf));
    if (n < 0) { out(C_BAD "cat: no such file: " C_RESET); out(name); out("\n"); return; }
    uint32_t len = (uint32_t)n;
    while (len && buf[len - 1] == '\0') --len;       /* SANDFS pads the sector */
    (void)sand_write(buf, len); out("\n");
}

static void cmd_write(char *argv[], uint32_t argc, const char *raw)
{
    if (argc < 3) { out(C_WARN "usage: write <file> <text>" C_RESET "\n"); return; }
    /* Keep the original spacing of the text: skip past the file name in raw. */
    const char *p = raw;
    while (*p == ' ') ++p;
    p += strlen(argv[0]);
    while (*p == ' ') ++p;
    p += strlen(argv[1]);
    while (*p == ' ') ++p;
    uint32_t len = strlen(p);
    if (len == 0 || len > 500) { out(C_BAD "write: text must be 1-500 bytes" C_RESET "\n"); return; }
    if (sand_fs_write(argv[1], p, len) != 0) { out(C_BAD "write: failed (name too long or disk full)" C_RESET "\n"); return; }
    out(C_GOOD "saved " C_RESET); sand_putu(len); out(" bytes to "); out(argv[1]); out("\n");
}

static void cmd_apps(void)
{
    struct sand_dirent e;
    for (uint32_t i = 0; sand_app_list(i, &e) == 0; ++i) { out("  " C_ACCENT); out(e.name); out(C_RESET "\n"); }
}

static const char *state_name(uint32_t s)
{
    switch (s) { case 1: return "ready"; case 2: return "running"; case 3: return "exited"; default: return "?"; }
}
static void cmd_ps(void)
{
    struct sand_procinfo p;
    out(C_MUTED "  PID  PPID  STATE     NAME" C_RESET "\n");
    for (uint32_t i = 0; sand_proc_list(i, &p) == 0; ++i) {
        char num[12];
        out("  "); sand_utoa(p.pid, num); out(num); pad(strlen(num), 5);
        if (p.parent_pid == 0xFFFFFFFFu) { out("-     "); }
        else { sand_utoa(p.parent_pid, num); out(num); pad(strlen(num), 6); }
        out(state_name(p.state)); pad(strlen(state_name(p.state)), 10); out(p.name); out("\n");
    }
}

static void cmd_mem(void)
{
    struct sand_sysinfo si;
    if (sand_sysinfo(&si) != 0) return;
    line_of("free memory", si.free_frames * 4u, " KiB");
    line_of("total memory", si.total_frames * 4u, " KiB managed");
    line_of("kernel heap", si.heap_used / 1024u, " KiB used");
    line_of("context switches", si.context_switches, "");
    line_of("processes", si.process_count, "");
}

static void cmd_uptime(void)
{
    struct sand_sysinfo si;
    if (sand_sysinfo(&si) != 0) return;
    uint32_t s = si.ticks / si.tick_hz;
    sand_putu(s / 60u); out(" min "); sand_putu(s % 60u); out(" s  ");
    out(C_MUTED "(" ); sand_putu(si.ticks); out(" ticks @ "); sand_putu(si.tick_hz); out(" Hz)" C_RESET "\n");
}

static void cmd_devices(void)
{
    struct sand_devinfo d;
    out(C_MUTED "  NAME      KIND       DRIVER          STATE" C_RESET "\n");
    for (uint32_t i = 0; sand_device_list(i, &d) == 0; ++i) {
        out("  " C_ACCENT); out(d.name); pad(strlen(d.name), 10); out(C_RESET);
        out(d.kind); pad(strlen(d.kind), 11); out(d.driver); pad(strlen(d.driver), 16);
        if (d.status == 1) out(C_GOOD "ready" C_RESET);
        else if (d.status == 2) out(C_BAD "failed" C_RESET);
        else out(C_MUTED "absent" C_RESET);
        out("\n");
    }
}

static void cmd_run(const char *app)
{
    int32_t pid = sand_spawn(app);
    if (pid < 0) { out(C_BAD "run: cannot start '" C_RESET); out(app); out(C_BAD "'" C_RESET "\n"); return; }
    int32_t status = 0;
    if (sand_wait(pid, &status) != 0) { out(C_BAD "run: wait failed" C_RESET "\n"); return; }
    out(C_MUTED "[pid "); sand_putu((uint32_t)pid);
    if (status <= SAND_EXIT_FAULT_BASE) {
        out(C_RESET C_BAD " killed by CPU fault, vector "); sand_putu((uint32_t)(SAND_EXIT_FAULT_BASE - status));
        out(C_RESET C_MUTED "; the system is unaffected]" C_RESET "\n");
    } else {
        out(" exited with status "); sand_puti(status); out("]" C_RESET "\n");
    }
}

static void prompt(void) { out(C_ACCENT "sand" C_MUTED " > " C_RESET); }

/* Returns 1 when the shell should exit. */
static int execute(char *line)
{
    char raw[LINE_MAX + 1];
    uint32_t n = strlen(line);
    memcpy(raw, line, n + 1);
    char *argv[ARG_MAX];
    uint32_t argc = split(line, argv, ARG_MAX);
    if (argc == 0) return 0;
    const char *c = argv[0];
    if (!strcmp(c, "help")) cmd_help();
    else if (!strcmp(c, "version")) cmd_version();
    else if (!strcmp(c, "echo")) {
        const char *p = raw; while (*p == ' ') ++p; p += 4; while (*p == ' ') ++p;
        out(p); out("\n");
    }
    else if (!strcmp(c, "clear")) out("\x1b[2J\x1b[H");
    else if (!strcmp(c, "ls")) cmd_ls();
    else if (!strcmp(c, "cat")) { if (argc < 2) out(C_WARN "usage: cat <file>" C_RESET "\n"); else cmd_cat(argv[1]); }
    else if (!strcmp(c, "write")) cmd_write(argv, argc, raw);
    else if (!strcmp(c, "rm")) {
        if (argc < 2) out(C_WARN "usage: rm <file>" C_RESET "\n");
        else if (sand_fs_remove(argv[1]) == 0) out(C_GOOD "removed " C_RESET), out(argv[1]), out("\n");
        else out(C_BAD "rm: no such file: " C_RESET), out(argv[1]), out("\n");
    }
    else if (!strcmp(c, "apps")) cmd_apps();
    else if (!strcmp(c, "run")) { if (argc < 2) out(C_WARN "usage: run <app>" C_RESET "\n"); else cmd_run(argv[1]); }
    else if (!strcmp(c, "ps")) cmd_ps();
    else if (!strcmp(c, "mem")) cmd_mem();
    else if (!strcmp(c, "uptime")) cmd_uptime();
    else if (!strcmp(c, "devices")) cmd_devices();
    else if (!strcmp(c, "sleep")) {
        uint32_t ms = 0;
        if (argc >= 2) for (const char *d = argv[1]; *d >= '0' && *d <= '9'; ++d) ms = ms * 10u + (uint32_t)(*d - '0');
        sand_sleep_ms(ms > 10000u ? 10000u : ms);
    }
    else if (!strcmp(c, "exit")) { out(C_MUTED "session ended" C_RESET "\n"); return 1; }
    else { out(C_BAD "sandsh: unknown command: " C_RESET); out(c); out(C_MUTED "  (try 'help')" C_RESET "\n"); }
    return 0;
}

void _start(void)
{
    char line[LINE_MAX + 1];
    uint32_t len = 0;
    out("\n" C_ACCENT "SAND" C_RESET " native shell  " C_MUTED "type 'help' for commands" C_RESET "\n");
    prompt();
    for (;;) {
        uint8_t ch;
        if (sand_read(&ch, 1) != 1) continue;
        if (ch == '\n' || ch == '\r') {
            out("\n"); line[len] = '\0';
            int quit = execute(line);
            len = 0;
            if (quit) break;
            prompt();
        } else if (ch == 0x08 || ch == 0x7F) {
            if (len) { --len; out("\b \b"); }
        } else if (ch == 0x03) {                       /* Ctrl-C: abandon the line */
            len = 0; out("^C\n"); prompt();
        } else if (ch == 0x15) {                       /* Ctrl-U: erase the line */
            while (len) { --len; out("\b \b"); }
        } else if (ch == 0x0C) {                       /* Ctrl-L: clear, keep the line */
            out("\x1b[2J\x1b[H"); prompt(); (void)sand_write(line, len);
        } else if (ch >= 0x20 && ch < 0x7F && len < LINE_MAX) {
            line[len++] = (char)ch; (void)sand_write((const char *)&ch, 1);
        }
    }
    sand_exit(0);
}
