#ifndef SAND_KERNEL_PERMS_H
#define SAND_KERNEL_PERMS_H
/* Per-process capability bits, derived at build time from apps/<name>/manifest.json "permissions"
 * (tools/gen_app_table.py). Checked by the syscall dispatcher; a missing bit returns SAND_E_PERM.
 * A process whose name is not in the built-in app table gets no bits (fail closed). */
#define SAND_PERM_CONSOLE_WRITE 0x01u   /* console.write (or console.rw) */
#define SAND_PERM_CONSOLE_READ  0x02u   /* console.read  (or console.rw) */
#define SAND_PERM_FS_READ       0x04u   /* fs.read  (or fs.rw) */
#define SAND_PERM_FS_WRITE      0x08u   /* fs.write (or fs.rw): write and remove */
#define SAND_PERM_SPAWN         0x10u   /* process.spawn */
#define SAND_PERM_WINDOW        0x20u   /* window.create */
#define SAND_PERM_SETTINGS      0x40u   /* settings.write */
#define SAND_PERM_INSTALL       0x80u   /* system.install: stage and store apps in SANDFS */
#define SAND_PERM_NET           0x200u  /* net.access: DRIFTNET ping, UDP send/receive, NIC info (audited on egress) */
#define SAND_PERM_IPC           0x100u  /* ipc.channel: open TIDEWAY channels, send and receive */
#endif
