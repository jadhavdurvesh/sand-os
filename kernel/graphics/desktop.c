#include "sand/graphics/desktop.h"
#include "sand/graphics/ui.h"
#include "sand/arch/x86/input.h"
#include "sand/arch/x86/rtc.h"
#include "sand/arch/x86/timer.h"
#include "sand/kernel/scheduler.h"
#include "sand/kernel/process.h"
#include "sand/console/console.h"
#include "sand/mm/frame.h"
#include "sand/mm/heap.h"

static sand_ui_t ui;
static volatile int desktop_ready;
static sand_framebuffer_t fb;
static volatile uint32_t frames_presented;
static volatile uint32_t pointer_events;

static void debug_text(const char *s) { while (*s) __asm__ volatile("outb %0,%1" : : "a"(*s++), "Nd"((uint16_t)0xE9)); }
static int host_clock(int *h, int *m) { return rtc_read_time(h, m, 0); }
static void host_sysinfo(struct sand_ui_sysinfo *s) {
    s->ticks = timer_ticks(); s->tick_hz = SAND_TICK_HZ; s->free_frames = frame_free_count(); s->total_frames = SAND_FRAME_LIMIT;
    s->heap_used = kheap_used(); s->heap_total = 0x00C00000u; s->switches = scheduler_switch_count(); s->processes = process_live_count();
}
static uint32_t host_pid(void) { return process_current(); }
static int host_pid_alive(uint32_t pid) { struct sand_process_info info; return process_get_info(pid, &info) == 0 && info.state != SAND_PROCESS_TERMINATED; }
static void console_to_ui(const char *data, uint32_t length) { sand_ui_term_write(&ui, data, length); }

static void present_frame(void) {
    struct sand_ui_rect r;
    if (sand_ui_frame(&ui, &r)) {
        sand_ui_present(&ui, fb.pixels, fb.pitch_bytes, fb.bits_per_pixel, &r);
        if (++frames_presented == 1u) debug_text("SAND_TEST:M11_DESKTOP_FIRST_FRAME\n");
    }
}
static void desktop_thread(void *arg) {
    (void)arg;
    for (;;) {
        sand_input_event_t e; int moved = 0; int32_t dx = 0, dy = 0; uint8_t buttons = ui.buttons;
        while (ps2_input_poll(&e)) {
            if (e.type == SAND_INPUT_KEY) {
                if (e.pressed && e.ascii != 0u && !sand_ui_key(&ui, (uint8_t)e.ascii) && e.ascii < 0x80u) console_input_push((uint8_t)e.ascii);
                continue;
            }
            if (e.type != SAND_INPUT_POINTER) continue;
            dx += e.mouse_dx; dy += e.mouse_dy; buttons = e.buttons; moved = 1;
            if (e.buttons != ui.buttons || dx > 40 || dx < -40) { sand_ui_pointer(&ui, dx, dy, buttons); dx = dy = 0; moved = 0; }
        }
        if (moved || buttons != ui.buttons) { sand_ui_pointer(&ui, dx, dy, buttons); if (++pointer_events == 1u) debug_text("SAND_TEST:M11_POINTER_EVENT\n"); }
        present_frame();
        __asm__ volatile("sti; hlt");
    }
}

int sand_desktop_start(const sand_framebuffer_t *framebuffer) {
    if (!graphics_framebuffer_valid(framebuffer) || (framebuffer->bits_per_pixel != 24u && framebuffer->bits_per_pixel != 32u)) return -1;
    fb = *framebuffer;
    sand_ui_host_t host = { kmalloc, timer_ticks, host_clock, host_sysinfo, host_pid, host_pid_alive };
    if (sand_ui_init(&ui, &host, (int32_t)fb.width, (int32_t)fb.height) != 0) return -2;
    present_frame();
    if (console_add_sink(console_to_ui) < 0) return -3;
    ps2_input_set_console_forward(0);
    desktop_ready = 1;
    if (scheduler_create(desktop_thread, 0) < 0) return -4;
    return 0;
}
uint32_t sand_desktop_frames(void) { return frames_presented; }

int sand_desktop_win_create(const char *title, int32_t w, int32_t h) { return desktop_ready ? sand_ui_win_create(&ui, process_current(), title, w, h) : SAND_E_NOSYS; }
int sand_desktop_win_draw(int32_t id, const struct sand_draw *cmds, uint32_t count) { return desktop_ready ? sand_ui_win_draw(&ui, id, process_current(), cmds, count) : SAND_E_NOSYS; }
int sand_desktop_win_event(int32_t id, struct sand_event *out) { return desktop_ready ? sand_ui_win_event(&ui, id, process_current(), out) : SAND_E_NOSYS; }
int sand_desktop_win_close(int32_t id) { return desktop_ready ? sand_ui_win_close(&ui, id, process_current()) : SAND_E_NOSYS; }
