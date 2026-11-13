#ifndef SAND_ABI_H
#define SAND_ABI_H
#include <stdint.h>

/* SAND system-call ABI v2. This header is the single source of truth: the
 * kernel and the application SDK both include it. Pure constants and
 * structures only - no inline assembly - so it is usable from host tests.
 *
 * Calling convention (x86-32): int 0x80, number in EAX, arguments in
 * EBX, ECX, EDX, result in EAX. Negative results are errors. */
#define SAND_ABI_VERSION 3u

enum sand_syscall_number {
    SAND_SYS_EXIT = 1,        /* exit(status)                         */
    SAND_SYS_WRITE = 2,       /* write(buf, len, reserved) -> bytes   */
    SAND_SYS_GET_TICKS = 3,   /* ticks() -> 100 Hz monotonic counter  */
    SAND_SYS_GETPID = 4,      /* getpid() -> pid                      */
    SAND_SYS_YIELD = 5,       /* yield() -> 0                         */
    SAND_SYS_SLEEP_MS = 6,    /* sleep(ms) -> 0                       */
    SAND_SYS_READ = 7,        /* read(buf, cap) -> bytes, blocks      */
    SAND_SYS_FS_READ = 8,     /* fs_read(name, buf, cap) -> bytes     */
    SAND_SYS_FS_WRITE = 9,    /* fs_write(name, buf, len) -> 0        */
    SAND_SYS_FS_LIST = 10,    /* fs_list(index, sand_dirent*) -> 0    */
    SAND_SYS_FS_REMOVE = 11,  /* fs_remove(name) -> 0                 */
    SAND_SYS_SPAWN = 12,      /* spawn(app_name) -> pid               */
    SAND_SYS_WAIT = 13,       /* wait(pid, int32_t *status) -> 0      */
    SAND_SYS_SYSINFO = 14,    /* sysinfo(sand_sysinfo*) -> 0          */
    SAND_SYS_PROC_LIST = 15,  /* proc_list(index, sand_procinfo*) -> 0*/
    SAND_SYS_APP_LIST = 16,   /* app_list(index, sand_dirent*) -> 0   */
    SAND_SYS_DEVICE_LIST = 17, /* device_list(index, sand_devinfo*) -> 0 */
    /* Window service (ABI v3). A window is a kernel-owned surface that the app
     * paints with SAND draw commands (same typeface and anti-aliased shapes as
     * the desktop) and that the desktop composites with its own chrome. */
    SAND_SYS_WIN_CREATE = 18, /* win_create(title, (w<<16)|h) -> window id (>0) */
    SAND_SYS_WIN_DRAW = 19,   /* win_draw(id, sand_draw*, count<=64) -> 0       */
    SAND_SYS_WIN_EVENT = 20,  /* win_event(id, sand_event*) -> 1 event, 0 none, <0 window gone */
    SAND_SYS_WIN_CLOSE = 21   /* win_close(id) -> 0                              */
};

#define SAND_E_FAIL   (-1)    /* generic failure / bad argument */
#define SAND_E_FAULT  (-2)    /* user pointer rejected by kernel */
#define SAND_E_NOENT  (-3)    /* no such file, app or process    */
#define SAND_E_NOSYS  (-4)    /* unknown system call             */

/* Exit status given to a process killed by a CPU fault:
 * SAND_EXIT_FAULT_BASE - vector (page fault 14 -> -270). */
#define SAND_EXIT_FAULT_BASE (-256)
/* ABI self-test token: exit(SAND_ABI_TOKEN) from ring 3 is recognised by
 * the kernel boot self-test as the original M4 privilege-transition proof. */
#define SAND_ABI_TOKEN 0x53414E44u

/* Window service types. Surface size limit: 200..520 x 120..380 pixels. */
#define SAND_WIN_MAX_W 520
#define SAND_WIN_MAX_H 380
#define SAND_WIN_MAX_DRAW 64u
enum { SAND_DRAW_CLEAR = 1, SAND_DRAW_RECT, SAND_DRAW_RRECT, SAND_DRAW_RING, SAND_DRAW_CIRCLE,
       SAND_DRAW_LINE, SAND_DRAW_TEXT, SAND_DRAW_SHADOW };
enum { SAND_FACE_SANS = 0, SAND_FACE_MEDIUM, SAND_FACE_SMALL, SAND_FACE_LARGE, SAND_FACE_MONO };
enum { SAND_ALIGN_LEFT = 0, SAND_ALIGN_CENTER, SAND_ALIGN_RIGHT };
/* One 64-byte draw command. Colours are 0x00RRGGBB; alpha 0 means opaque.
 * RRECT: aux=1 -> vertical gradient color->color2.  RING: aux=thickness.
 * LINE: (x,y)-(w,h) endpoints, aux=width in 1/16 px.  SHADOW: aux=blur, r=radius.
 * TEXT: (x,y)=anchor/baseline, face, align, text[]. CIRCLE: x,y centre, r radius. */
struct sand_draw {
    uint8_t op, face, align, alpha;
    int16_t x, y, w, h, r, aux;
    uint32_t color, color2;
    char text[40];
};
enum { SAND_EV_KEY = 1, SAND_EV_MOUSE_DOWN, SAND_EV_MOUSE_UP, SAND_EV_MOUSE_MOVE };
#ifndef SAND_KEY_UP
#define SAND_KEY_UP 0x80u
#define SAND_KEY_DOWN 0x81u
#define SAND_KEY_LEFT 0x82u
#define SAND_KEY_RIGHT 0x83u
#endif
/* Pointer coordinates are relative to the window's surface origin. */
struct sand_event { uint32_t type; int32_t x, y; uint32_t key; uint32_t buttons; };

#define SAND_NAME_MAX 24u
struct sand_dirent { char name[SAND_NAME_MAX]; uint32_t length; };
struct sand_sysinfo {
    uint32_t abi_version, ticks, tick_hz;
    uint32_t free_frames, total_frames, heap_used;
    uint32_t context_switches, process_count;
};
struct sand_devinfo { char name[16]; char kind[12]; char driver[16]; uint32_t status; };
struct sand_procinfo { uint32_t pid, parent_pid, state; int32_t exit_code; char name[16]; };
#endif
