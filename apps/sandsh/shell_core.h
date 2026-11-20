/* Shared command interpreter for sandsh (console) and CAIRN (window).
 * The including file must define sh_write(), sh_puts() and sh_putu() first. */

#define LINE_MAX 200u
#define ARG_MAX 8u

/* Palette from the SAND design system, as truecolor SGR sequences. */
#define C_RESET  "\x1b[0m"
#define C_ACCENT "\x1b[38;2;169;155;255m"
#define C_MUTED  "\x1b[38;2;146;153;173m"
#define C_GOOD   "\x1b[38;2;130;200;173m"
#define C_WARN   "\x1b[38;2;215;189;141m"
#define C_BAD    "\x1b[38;2;217;137;145m"

static void out(const char *s) { sh_puts(s); }
static void pad(uint32_t used, uint32_t width) { while (used++ < width) out(" "); }
static void line_of(const char *label, uint32_t value, const char *unit)
{
    out(C_MUTED); out(label); pad(strlen(label), 18); out(C_RESET);
    sh_putu(value); out(unit); out("\n");
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
        {"install", "install <name>: store an app sent as hex lines"},
        {"cat <file>", "print a file"},
        {"write <file> <text>", "create or replace a file"},
        {"rm <file>", "remove a file"},
        {"apps", "list installed applications"},
        {"run <app>", "launch an app and wait for it"},
        {"open <app>", "launch an app in the background"},
        {"settings [k v]", "show or change system settings"},
        {"ps", "list processes"},
        {"veyra", "list the Veyra subsystem interfaces this machine offers"},
        {"net", "network card address and counters (DRIFTNET)"},
        {"huginn <name> [server]", "look a host name up (HUGINN resolver)"},
        {"ping <a.b.c.d>", "send an ICMP echo and time the reply"},
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
    if (sand_sysinfo(&si) == 0) { out("syscall ABI v"); sh_putu(si.abi_version); out("\n"); }
}

static void cmd_ls(void)
{
    struct sand_dirent e;
    uint32_t n = 0;
    for (uint32_t i = 0; sand_fs_list(i, &e) == 0; ++i) {
        out("  " C_ACCENT); out(e.name); pad(strlen(e.name), 26); out(C_RESET);
        sh_putu(e.length); out(" bytes\n"); ++n;
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
    (void)sh_write(buf, len); out("\n");
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
    out(C_GOOD "saved " C_RESET); sh_putu(len); out(" bytes to "); out(argv[1]); out("\n");
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

static const char *veyra_name(uint32_t id)
{
    return id == SAND_VEYRA_IF_FILES ? "FILES   (BEDROCK)" : id == SAND_VEYRA_IF_IPC ? "IPC     (TIDEWAY)" :
           id == SAND_VEYRA_IF_NOTIFY ? "NOTIFY  (HERALD)" : id == SAND_VEYRA_IF_NET ? "NET     (DRIFTNET)" : "NAMES   (HUGINN)";
}
static void cmd_veyra(void)
{
    static const uint32_t ids[] = { SAND_VEYRA_IF_FILES, SAND_VEYRA_IF_IPC, SAND_VEYRA_IF_NOTIFY, SAND_VEYRA_IF_NET, SAND_VEYRA_IF_NAMES };
    out(C_MUTED "  ID      INTERFACE           REV  PERM  LIMITS" C_RESET "\n");
    for (uint32_t i = 0; i < 5; ++i) {
        struct sand_veyra_subsystem_abi v;
        int32_t rc = sand_veyra_query(ids[i], &v);
        out("  0x0"); sh_putu(ids[i] >> 8); out("0"); sh_putu(ids[i] & 15u); out("  "); out(veyra_name(ids[i])); pad(strlen(veyra_name(ids[i])), 20);
        if (rc != 0) { out(C_WARN "not present" C_RESET "\n"); continue; }
        sh_putu(v.revision); out("    "); sh_putu(v.permission); out("    ");
        for (uint32_t k = 0; k < 4; ++k) { sh_putu(v.limit[k]); out(k < 3 ? " / " : "\n"); }
    }
}
static void print_ip(const uint8_t *ip) { for (int i = 0; i < 4; ++i) { sh_putu(ip[i]); if (i < 3) out("."); } }
static void cmd_net(void)
{
    struct sand_netinfo ni;
    int32_t rc = sand_net_info(&ni);
    if (rc == SAND_E_PERM) { out(C_BAD "net: this shell has no net.access permission" C_RESET "\n"); return; }
    if (rc != 0) { out(C_WARN "net: no network card" C_RESET "\n"); return; }
    static const char hex[] = "0123456789abcdef";
    out("  mac      "); for (int i = 0; i < 6; ++i) { char b[3] = { hex[ni.mac[i] >> 4], hex[ni.mac[i] & 15], 0 }; out(b); if (i < 5) out(":"); }
    out("\n  ip       "); print_ip(ni.ip); out("\n  netmask  "); print_ip(ni.mask); out("\n  gateway  "); print_ip(ni.gateway); out((ni.flags & 1u) ? "\n  address  from DHCP" : "\n  address  static fallback");
    out("\n  frames   rx "); sh_putu(ni.rx_frames); out("  tx "); sh_putu(ni.tx_frames); out("\n");
}
static int parse_ip_port(const char *s, uint32_t *ip, uint32_t *port)
{
    uint32_t a[4] = {0}, k = 0, digits = 0, p = 0; int has_port = 0;
    for (; *s; ++s) {
        if (*s >= '0' && *s <= '9') {
            if (has_port) { p = p * 10u + (uint32_t)(*s - '0'); if (p > 65535u) return 0; }
            else { a[k] = a[k] * 10u + (uint32_t)(*s - '0'); if (a[k] > 255u) return 0; }
            ++digits;
        } else if (*s == '.' && !has_port && digits && k < 3) { ++k; digits = 0; }
        else if (*s == ':' && !has_port && digits && k == 3) { has_port = 1; digits = 0; }
        else return 0;
    }
    if (k != 3 || !digits) { if (!(has_port && digits)) return 0; }
    *ip = a[0] | (a[1] << 8) | (a[2] << 16) | (a[3] << 24); *port = p; return 1;
}
static void cmd_huginn(int argc, char **argv)
{
    struct sand_resolve r; memset(&r, 0, sizeof(r));
    if (argc < 2 || strlen(argv[1]) > 63) { out(C_WARN "usage: huginn <name> [server a.b.c.d[:port]]" C_RESET "\n"); return; }
    for (uint32_t k = 0; argv[1][k]; ++k) r.name[k] = argv[1][k];
    if (argc > 2) {                                  /* optional resolver: a.b.c.d or a.b.c.d:port */
        uint32_t ip = 0, port = 0;
        if (!parse_ip_port(argv[2], &ip, &port)) { out(C_WARN "huginn: bad server address" C_RESET "\n"); return; }
        r.server = ip; r.port = (uint16_t)port;
    }
    int32_t rc = sand_name_resolve(&r);
    if (rc == 0) { out(argv[1]); out(" is "); print_ip((const uint8_t *)&r.ip); if (r.server_used) { out("  (via "); print_ip((const uint8_t *)&r.server_used); out(")\n"); } else out("  (cached)\n"); }
    else if (rc == SAND_E_NODEV) out(C_WARN "huginn: no such name" C_RESET "\n");
    else if (rc == SAND_E_TIMEOUT) out(C_WARN "huginn: no answer (timeout)" C_RESET "\n");
    else if (rc == SAND_E_PERM) out(C_BAD "huginn: this shell has no net.access permission" C_RESET "\n");
    else out(C_BAD "huginn: bad name or lookup failed" C_RESET "\n");
}
static void cmd_ping(const char *arg)
{
    uint32_t ip = 0, part = 0, dots = 0, digits = 0;
    for (const char *p = arg;; ++p) {
        if (*p >= '0' && *p <= '9') { part = part * 10u + (uint32_t)(*p - '0'); if (++digits > 3 || part > 255u) { dots = 99; break; } }
        else if ((*p == '.' || *p == 0) && digits) { ip |= part << (8u * dots); part = 0; digits = 0; if (*p == 0) break; if (++dots > 3) { dots = 99; break; } }
        else { dots = 99; break; }
    }
    if (dots != 3) { out(C_WARN "usage: ping <a.b.c.d>" C_RESET "\n"); return; }
    int32_t rc = sand_net_ping(ip, 3000);
    if (rc >= 0) { out("reply from "); out(arg); out(": time="); sh_putu((uint32_t)rc); out(" ms\n"); }
    else if (rc == SAND_E_TIMEOUT) out(C_WARN "ping: no reply (timeout)" C_RESET "\n");
    else if (rc == SAND_E_PERM) out(C_BAD "ping: this shell has no net.access permission" C_RESET "\n");
    else if (rc == SAND_E_NODEV) out(C_WARN "ping: no network card" C_RESET "\n");
    else out(C_BAD "ping: failed" C_RESET "\n");
}
static void cmd_uptime(void)
{
    struct sand_sysinfo si;
    if (sand_sysinfo(&si) != 0) return;
    uint32_t s = si.ticks / si.tick_hz;
    sh_putu(s / 60u); out(" min "); sh_putu(s % 60u); out(" s  ");
    out(C_MUTED "(" ); sh_putu(si.ticks); out(" ticks @ "); sh_putu(si.tick_hz); out(" Hz)" C_RESET "\n");
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
    out(C_MUTED "[pid "); sh_putu((uint32_t)pid);
    if (status <= SAND_EXIT_FAULT_BASE) {
        out(C_RESET C_BAD " killed by CPU fault, vector "); sh_putu((uint32_t)(SAND_EXIT_FAULT_BASE - status));
        out(C_RESET C_MUTED "; the system is unaffected]" C_RESET "\n");
    } else {
        out(" exited with status "); sand_puti(status); out("]" C_RESET "\n");
    }
}

#define OPEN_MAX 8
static int32_t opened[OPEN_MAX];
static void reap_opened(void)          /* collect finished background apps so their slots are reusable */
{
    for (int i = 0; i < OPEN_MAX; ++i) {
        if (opened[i] <= 0) continue;
        struct sand_procinfo info; int found = 0;
        for (uint32_t n = 0; sand_proc_list(n, &info) == 0; ++n)
            if ((int32_t)info.pid == opened[i]) { found = 1; if (info.state == 3u) { int32_t st; (void)sand_wait(opened[i], &st); opened[i] = 0; } break; }
        if (!found) opened[i] = 0;
    }
}
static void cmd_settings(char *argv[], uint32_t argc)
{
    static const char *const keys[3] = { "accent", "motion", "clock24" };
    if (argc == 3) {
        for (uint32_t k = 0; k < 3; ++k)
            if (!strcmp(argv[1], keys[k])) {
                uint32_t v = 0; for (const char *p = argv[2]; *p >= '0' && *p <= '9'; ++p) v = v * 10u + (uint32_t)(*p - '0');
                if (sand_setting_set(k + 1u, v) != 0) out(C_WARN "rejected: bad value" C_RESET "\n");
                return;
            }
        out(C_WARN "usage: settings [accent|motion|clock24 <value>]" C_RESET "\n"); return;
    }
    for (uint32_t k = 0; k < 3; ++k) { out(keys[k]); out("="); sh_putu((uint32_t)sand_setting_get(k + 1u)); out("\n"); }
}
static void cmd_open(const char *app)
{
    reap_opened();
    int slot = -1; for (int i = 0; i < OPEN_MAX; ++i) if (opened[i] <= 0) { slot = i; break; }
    int32_t pid = slot < 0 ? -1 : sand_spawn(app);
    if (pid < 0) { out(C_BAD "open: cannot start '" C_RESET); out(app); out(C_BAD "'" C_RESET "\n"); return; }
    opened[slot] = pid;
    out(C_MUTED "[pid "); sh_putu((uint32_t)pid); out(" started]" C_RESET "\n");
}
/* ---- install: receive an app image as hex lines (<=100 bytes each) and hand it to the kernel installer ---- */
static int g_inst_active; static uint32_t g_inst_off; static char g_inst_name[17];
static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static void inst_fail(const char *why, int32_t code)
{
    out(C_BAD "install: "); out(why); out(" ("); sand_puti(code); out(")" C_RESET "\n");
    g_inst_active = 0;
}
static void inst_line(const char *line)
{
    while (*line == ' ') ++line;
    if (!strcmp(line, "abort")) { g_inst_active = 0; out(C_MUTED "install: aborted" C_RESET "\n"); return; }
    if (!strcmp(line, "end")) {
        int32_t rc = sand_install_commit(g_inst_name);
        if (rc != 0) { inst_fail("the kernel refused the image (bad format, built-in name, or launch policy)", rc); return; }
        g_inst_active = 0;
        out(C_GOOD "installed " C_RESET); out(g_inst_name); out(" ("); sh_putu(g_inst_off); out(" bytes); run it with: run "); out(g_inst_name); out("\n");
        return;
    }
    uint8_t buf[100]; uint32_t n = 0, len = strlen(line);
    if (len == 0 || (len & 1u) || len / 2 > sizeof(buf)) { inst_fail("bad hex line", -1); return; }
    for (uint32_t i = 0; i < len; i += 2) {
        int hi = hexval(line[i]), lo = hexval(line[i + 1]);
        if (hi < 0 || lo < 0) { inst_fail("bad hex digit", -1); return; }
        buf[n++] = (uint8_t)(hi * 16 + lo);
    }
    int32_t rc = sand_install_put(g_inst_off, buf, n);
    if (rc < 0) { inst_fail("could not stage the data", rc); return; }
    g_inst_off += n; out(".");
}
static void cmd_install(const char *name)
{
    uint32_t n = strlen(name);
    if (n == 0 || n > 16) { out(C_BAD "usage: install <name>   (then send the .sapp as hex lines, finish with 'end')" C_RESET "\n"); return; }
    memcpy(g_inst_name, name, n + 1);
    int32_t rc = sand_install_put(0, g_inst_name, 0);
    if (rc < 0) { inst_fail(rc == SAND_E_PERM ? "this app may not install apps (needs system.install)" : "installer busy", rc); return; }
    g_inst_active = 1; g_inst_off = 0;
    out(C_MUTED "install: send hex lines (up to 200 characters each), then 'end' (or 'abort')" C_RESET "\n");
}
static void prompt(void) { reap_opened(); if (g_inst_active) return; out(C_ACCENT "sand" C_MUTED " > " C_RESET); }

/* Returns 1 when the shell should exit. */
static int execute(char *line)
{
    char raw[LINE_MAX + 1];
    uint32_t n = strlen(line);
    memcpy(raw, line, n + 1);
    if (g_inst_active) { inst_line(line); return 0; }
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
    else if (!strcmp(c, "install")) cmd_install(argc > 1 ? argv[1] : "");
    else if (!strcmp(c, "cat")) { if (argc < 2) out(C_WARN "usage: cat <file>" C_RESET "\n"); else cmd_cat(argv[1]); }
    else if (!strcmp(c, "write")) cmd_write(argv, argc, raw);
    else if (!strcmp(c, "rm")) {
        if (argc < 2) out(C_WARN "usage: rm <file>" C_RESET "\n");
        else if (sand_fs_remove(argv[1]) == 0) out(C_GOOD "removed " C_RESET), out(argv[1]), out("\n");
        else out(C_BAD "rm: no such file: " C_RESET), out(argv[1]), out("\n");
    }
    else if (!strcmp(c, "apps")) cmd_apps();
    else if (!strcmp(c, "settings")) cmd_settings(argv, argc);
    else if (!strcmp(c, "open")) { if (argc < 2) out(C_WARN "usage: open <app>" C_RESET "\n"); else cmd_open(argv[1]); }
    else if (!strcmp(c, "run")) { if (argc < 2) out(C_WARN "usage: run <app>" C_RESET "\n"); else cmd_run(argv[1]); }
    else if (!strcmp(c, "ps")) cmd_ps();
    else if (!strcmp(c, "veyra")) cmd_veyra();
    else if (!strcmp(c, "net")) cmd_net();
    else if (!strcmp(c, "huginn")) cmd_huginn(argc, argv);
    else if (!strcmp(c, "ping")) { if (argc < 2) out(C_WARN "usage: ping <a.b.c.d>" C_RESET "\n"); else cmd_ping(argv[1]); }
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

