/* jail: declares only console.write, then tries every gated capability. Used by the shell tests to prove
 * the kernel enforces manifest permissions (every attempt must return SAND_E_PERM, -5). */
#include "sand/libsand.h"
static void num(const char *label, int32_t v) {
    sand_puts(label);
    if (v < 0) { sand_puts("-"); v = -v; }
    sand_putu((uint32_t)v);
    sand_puts(" ");
}
void _start(void)
{
    char buf[8];
    sand_puts("jail: ");
    num("win=", sand_win_create("JAIL", 300, 200));
    num("set=", sand_setting_set(SAND_SET_ACCENT, 1));
    num("fsr=", sand_fs_read("system.cfg", buf, sizeof(buf)));
    num("fsw=", sand_fs_write("jail.txt", "x", 1));
    num("spawn=", sand_spawn("hello"));
    num("read=", sand_read(buf, sizeof(buf)));
    num("get=", sand_setting_get(SAND_SET_ACCENT) >= 0 ? 0 : -1);
    sand_puts("\n");
    sand_exit(0);
}
