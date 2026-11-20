#ifndef SAND_ABI_H
#define SAND_ABI_H
#include <stdint.h>

/* SAND system-call ABI v2. This header is the single source of truth: the
 * kernel and the application SDK both include it. Pure constants and
 * structures only - no inline assembly - so it is usable from host tests.
 *
 * Calling convention (x86-32): int 0x80, number in EAX, arguments in
 * EBX, ECX, EDX, result in EAX. Negative results are errors. */
#define SAND_ABI_VERSION 11u

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
    SAND_SYS_WIN_CLOSE = 21,  /* win_close(id) -> 0                              */
    SAND_SYS_WIN_BLIT = 22,   /* win_blit(id, sand_blit*) -> 0: copy an app-owned pixel buffer */
    SAND_SYS_WIN_RESIZABLE = 23, /* win_resizable(id, (min_w<<16)|min_h) -> 0: opt in to user resizing */
    SAND_SYS_SETTING_GET = 24,   /* setting_get(key) -> value >= 0, or SAND_E_FAIL for an unknown key   */
    SAND_SYS_SETTING_SET = 25,   /* setting_set(key, value) -> 0, or SAND_E_FAIL for a bad key/value; persisted to system.cfg */
    SAND_SYS_INSTALL_PUT = 26,   /* install_put(offset, data, len<=512) -> staged bytes; offset 0 starts a new image, later offsets must append */
    SAND_SYS_INSTALL_COMMIT = 27,/* install_commit(name) -> 0: gate-check the staged SAPP and store it as <name>.sapp (needs system.install) */
    SAND_SYS_TIME_GET = 28,      /* time_get(sand_time*) -> 0: the machine's real-time clock (UTC as set by firmware, unsynchronised), SAND_E_FAIL if unreadable */
    SAND_SYS_CHAN_OPEN = 29,     /* chan_open(name) -> handle >= 1: open (or create) a TIDEWAY channel; needs ipc.channel */
    SAND_SYS_CHAN_SEND = 30,     /* chan_send(handle, data, len 1..112) -> 0, SAND_E_FULL when the channel holds 8 messages */
    SAND_SYS_CHAN_RECV = 31,     /* chan_recv(handle, sand_chan_msg*) -> 1 message (oldest first; .from is the sender pid), 0 none */
    SAND_SYS_NAME_RESOLVE = 41,  /* name_resolve(sand_resolve*) -> 0 with .ip filled (blocks; server 0 = the DHCP/default resolver, port 0 = 53), SAND_E_TIMEOUT, SAND_E_NODEV when the name does not exist; needs net.access */
    SAND_SYS_TCP_CONNECT = 37,   /* tcp_connect(ip, port, timeout_ms) -> 0 once established (blocks), SAND_E_TIMEOUT, SAND_E_CLOSED when refused; one connection system-wide */
    SAND_SYS_TCP_SEND = 38,      /* tcp_send(data, len 1..512) -> len once the peer ACKed it (blocks), SAND_E_TIMEOUT, SAND_E_CLOSED */
    SAND_SYS_TCP_RECV = 39,      /* tcp_recv(buf, cap) -> bytes (non-blocking), 0 nothing yet, SAND_E_CLOSED when the peer closed and nothing is left */
    SAND_SYS_TCP_CLOSE = 40,     /* tcp_close() -> 0 */
    SAND_SYS_VEYRA_QUERY = 36,   /* veyra_query(interface_id, sand_veyra_subsystem_info*) -> 0, SAND_E_NODEV for unknown or absent subsystems; id 0 = count; open to every app */
    SAND_SYS_NET_INFO = 32,      /* net_info(sand_netinfo*) -> 0, SAND_E_NODEV without a NIC; needs net.access */
    SAND_SYS_NET_PING = 33,      /* net_ping(ip, timeout_ms) -> round-trip ms (blocks), SAND_E_TIMEOUT, SAND_E_NODEV; ip = bytes a,b,c,d as a little-endian u32 */
    SAND_SYS_NET_UDP_SEND = 34,  /* net_udp_send(sand_udp*) -> 0; data <= 512 bytes */
    SAND_SYS_NET_UDP_RECV = 35,  /* net_udp_recv(port, sand_udp*) -> 1 datagram, 0 none (non-blocking); the first call binds the port */
};

#define SAND_E_FAIL   (-1)    /* generic failure / bad argument */
#define SAND_E_FAULT  (-2)    /* user pointer rejected by kernel */
#define SAND_E_NOENT  (-3)    /* no such file, app or process    */
#define SAND_E_NOSYS  (-4)    /* unknown system call             */
#define SAND_E_PERM   (-5)    /* the app's manifest does not grant this capability */
#define SAND_E_FULL   (-6)    /* a bounded kernel table or queue is full (TIDEWAY channels) */

#define SAND_E_TIMEOUT (-7)   /* a network wait expired */
#define SAND_E_NODEV   (-8)   /* no such device (no NIC) */
#define SAND_E_CLOSED  (-9)   /* the TCP peer closed or reset the connection */
struct sand_netinfo { uint8_t mac[6]; uint8_t ip[4]; uint8_t gateway[4]; uint8_t mask[4]; uint32_t rx_frames; uint32_t tx_frames; uint32_t flags; };   /* flags bit 0: address came from DHCP */
struct sand_udp { uint32_t ip; uint16_t src_port; uint16_t dst_port; uint32_t length; uint8_t data[512]; };

struct sand_resolve { char name[64]; uint32_t server; uint16_t port; uint16_t pad; uint32_t ip; uint32_t ttl; uint32_t server_used; };   /* ip and server_used: bytes a,b,c,d as a little-endian u32 */

/* Veyra subsystem descriptor as returned by veyra_query (layout identical to include/sand/veyra/subsystems.h). */
struct sand_veyra_subsystem_abi {
    uint32_t magic; uint16_t abi_major, abi_minor; uint32_t struct_size, flags;   /* struct sand_veyra_interface */
    uint32_t interface_id, capabilities, revision, permission, limit[4];
};
#define SAND_VEYRA_IF_FILES  0x0101u
#define SAND_VEYRA_IF_IPC    0x0102u
#define SAND_VEYRA_IF_NOTIFY 0x0103u
#define SAND_VEYRA_IF_NET    0x0104u
#define SAND_VEYRA_IF_NAMES  0x0105u

/* TIDEWAY message as delivered by chan_recv. */
struct sand_chan_msg { uint32_t from; uint32_t length; uint8_t data[112]; };

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
enum { SAND_EV_KEY = 1, SAND_EV_MOUSE_DOWN, SAND_EV_MOUSE_UP, SAND_EV_MOUSE_MOVE,
       SAND_EV_RESIZE /* x = new client width, y = new client height; surface was cleared, redraw it */ };
#ifndef SAND_KEY_UP
#define SAND_KEY_UP 0x80u
#define SAND_KEY_DOWN 0x81u
#define SAND_KEY_LEFT 0x82u
#define SAND_KEY_RIGHT 0x83u
#endif
/* Pointer coordinates are relative to the window's surface origin. */
struct sand_event { uint32_t type; int32_t x, y; uint32_t key; uint32_t buttons; };

/* App-owned pixels: 0x00RRGGBB, tightly packed rows (stride = w). The kernel copies the
 * rectangle into the window surface at (x,y), clipped to the surface. w*h <= surface area.
 * scale (0 or 1..4): every source pixel becomes scale x scale surface pixels, so an app
 * with a small memory image can still fill a whole window. */
/* System appearance settings (KEYSTONE). Values are validated by the kernel. */
enum { SAND_SET_ACCENT = 1,          /* 0 champagne, 1 glacier, 2 violet, 3 jade, 4 ember */
       SAND_SET_REDUCED_MOTION = 2,  /* 0/1: skip window open/close animation */
       SAND_SET_CLOCK_24H = 3 };     /* 0/1: menu-bar clock format */
#define SAND_ACCENT_COUNT 5u

struct sand_blit { const uint32_t *pixels; int32_t x, y, w, h, scale; };

#define SAND_NAME_MAX 24u
struct sand_dirent { char name[SAND_NAME_MAX]; uint32_t length; };
struct sand_sysinfo {
    uint32_t abi_version, ticks, tick_hz;
    uint32_t free_frames, total_frames, heap_used;
    uint32_t context_switches, process_count;
};
struct sand_time { uint32_t year, month, day, hour, minute, second; };
struct sand_devinfo { char name[16]; char kind[12]; char driver[16]; uint32_t status; };
struct sand_procinfo { uint32_t pid, parent_pid, state; int32_t exit_code; char name[16]; };
#endif
