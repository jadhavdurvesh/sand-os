/* wardprobe: holds fs.read/fs.write but NOT system.install or settings.write, then tries to create files the MIMIR
 * wards protect. Used by tests/qemu/test_mimir.py: new .sapp files and system.cfg must be refused (SAND_E_PERM, -5). */
#include "sand/libsand.h"
static void num(const char *label, int32_t v) {
    sand_puts(label);
    if (v < 0) { sand_puts("-"); v = -v; }
    sand_putu((uint32_t)v);
    sand_puts(" ");
}
void _start(void)
{
    sand_puts("wardprobe: ");
    num("sapp=", sand_fs_write("probe.sapp", "x", 1));      /* ward: *.sapp needs system.install to create */
    num("txt=", sand_fs_write("probe.txt", "x", 1));        /* no ward matches: allowed */
    num("cfg=", sand_fs_write("system.cfg", "x", 1));       /* labelled settings.write */
    num("rm=", sand_fs_remove("probe.txt"));
    sand_puts("\n");
    sand_exit(0);
}
