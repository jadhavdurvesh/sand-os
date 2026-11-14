/* sandsh - the SAND native shell. Runs in ring 3 and talks to the kernel only
 * through the documented syscall ABI (sdk/sapp/include/sand/abi.h). */
#include "sand/libsand.h"

static void sh_write(const char *s, uint32_t n) { (void)sand_write(s, n); }
static void sh_puts(const char *s) { sand_puts(s); }
static void sh_putu(uint32_t v) { sand_putu(v); }
#include "shell_core.h"

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
            out("\x1b[2J\x1b[H"); prompt(); (void)sh_write(line, len);
        } else if (ch >= 0x20 && ch < 0x7F && len < LINE_MAX) {
            line[len++] = (char)ch; (void)sh_write((const char *)&ch, 1);
        }
    }
    sand_exit(0);
}
