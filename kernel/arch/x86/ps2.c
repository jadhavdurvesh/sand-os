#include "sand/arch/x86/input.h"
#include "sand/arch/x86/irq.h"
#include "sand/console/console.h"
#include "sand/drivers/device.h"
static volatile uint8_t console_forward=1u;   /* desktop clears this and routes keys itself */
void ps2_input_set_console_forward(int enable) { console_forward=enable?1u:0u; }

#define PS2_STATUS 0x64u
#define PS2_DATA 0x60u
#define EVENT_QUEUE 64u

static sand_input_event_t queue[EVENT_QUEUE];
static volatile uint32_t q_head,q_tail,dropped;
static uint8_t mouse_packet[3],mouse_index,mouse_buttons,modifiers,extended,mouse_ok;

static inline uint8_t in_port(uint16_t port) { uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(port)); return v; }
static inline void out_port(uint16_t port,uint8_t value) { __asm__ volatile("outb %0,%1"::"a"(value),"Nd"(port)); }
static int wait_input_clear(void) { for(uint32_t i=0;i<100000u;++i) if((in_port(PS2_STATUS)&2u)==0u) return 0; return -1; }
static int wait_output_full(void) { for(uint32_t i=0;i<100000u;++i) if((in_port(PS2_STATUS)&1u)!=0u) return 0; return -1; }
static int controller_command(uint8_t command) { if(wait_input_clear()!=0) return -1; out_port(PS2_STATUS,command); return 0; }
static int device_write(uint8_t value,int mouse) {
    if(mouse && controller_command(0xD4u)!=0) return -1;
    if(wait_input_clear()!=0) return -1;
    out_port(PS2_DATA,value);
    if(wait_output_full()!=0) return -1;
    return in_port(PS2_DATA)==0xFAu?0:-1;
}

/* US layout, scancode set 1 (the controller translates set 2 to set 1). */
static const uint8_t map_plain[0x3A]={
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s',
    'd','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v',
    'b','n','m',',','.','/',0,'*',0,' '};
static const uint8_t map_shift[0x3A]={
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S',
    'D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V',
    'B','N','M','<','>','?',0,'*',0,' '};
uint8_t ps2_decode_scancode(uint8_t sc,uint8_t mods) {
    if(sc>=0x3Au) return 0;
    uint8_t ch=(mods&SAND_MOD_SHIFT)?map_shift[sc]:map_plain[sc];
    if((mods&SAND_MOD_CAPS) && ch>='a' && ch<='z') ch=(uint8_t)(ch-'a'+'A');
    else if((mods&SAND_MOD_CAPS) && ch>='A' && ch<='Z' && (mods&SAND_MOD_SHIFT)) ch=(uint8_t)(ch-'A'+'a');
    if((mods&SAND_MOD_CTRL) && ch>='a' && ch<='z') ch=(uint8_t)(ch-'a'+1);
    else if((mods&SAND_MOD_CTRL) && ch>='A' && ch<='Z') ch=(uint8_t)(ch-'A'+1);
    return ch;
}
static inline void debug_marker(const char *text) { while(*text) __asm__ volatile("outb %0,$0xE9"::"a"(*text++)); }
static void enqueue(const sand_input_event_t *event) {
    static uint8_t seen_key,seen_pointer;
    if(event->type==SAND_INPUT_KEY && event->pressed && !seen_key) { seen_key=1u; debug_marker("SAND_TEST:M8_KEYBOARD_IRQ_PASS\n"); }
    if(event->type==SAND_INPUT_POINTER && !seen_pointer) { seen_pointer=1u; debug_marker("SAND_TEST:M8_MOUSE_IRQ_PASS\n"); }
    uint32_t next=(q_head+1u)%EVENT_QUEUE;
    if(next==q_tail) { ++dropped; return; }
    queue[q_head]=*event; q_head=next;
}
static uint8_t extended_code(uint8_t sc) {
    switch(sc) { case 0x48: return SAND_KEY_UP; case 0x50: return SAND_KEY_DOWN; case 0x4B: return SAND_KEY_LEFT;
                 case 0x4D: return SAND_KEY_RIGHT; case 0x47: return SAND_KEY_HOME; case 0x4F: return SAND_KEY_END;
                 case 0x53: return SAND_KEY_DELETE; default: return 0; }
}
static void keyboard_byte(uint8_t data) {
    if(data==0xE0u) { extended=1u; return; }
    uint8_t release=(data&0x80u)?1u:0u, sc=(uint8_t)(data&0x7Fu);
    uint8_t was_extended=extended; extended=0u;
    if(!was_extended) {
        if(sc==0x2A || sc==0x36) { modifiers=release?(uint8_t)(modifiers&~SAND_MOD_SHIFT):(uint8_t)(modifiers|SAND_MOD_SHIFT); return; }
        if(sc==0x1D) { modifiers=release?(uint8_t)(modifiers&~SAND_MOD_CTRL):(uint8_t)(modifiers|SAND_MOD_CTRL); return; }
        if(sc==0x38) { modifiers=release?(uint8_t)(modifiers&~SAND_MOD_ALT):(uint8_t)(modifiers|SAND_MOD_ALT); return; }
        if(sc==0x3A && !release) { modifiers^=SAND_MOD_CAPS; return; }
    } else if(sc==0x1D) { modifiers=release?(uint8_t)(modifiers&~SAND_MOD_CTRL):(uint8_t)(modifiers|SAND_MOD_CTRL); return; }
    sand_input_event_t e={0};
    e.type=SAND_INPUT_KEY; e.key=(uint16_t)sc|(was_extended?0x100u:0u); e.pressed=(uint8_t)!release; e.modifiers=modifiers; e.buttons=mouse_buttons;
    if(was_extended) e.ascii=extended_code(sc);
    else if(sc>=0x3Bu && sc<=0x44u) e.ascii=(uint8_t)(SAND_KEY_F1+(sc-0x3Bu));
    else e.ascii=ps2_decode_scancode(sc,modifiers);
    enqueue(&e);
    if(console_forward && !release && e.ascii!=0u && e.ascii<0x80u) console_input_push(e.ascii);
}
static void mouse_byte(uint8_t data) {
    if(mouse_index==0u && (data&0x08u)==0u) return;     /* resync on the always-set bit */
    mouse_packet[mouse_index++]=data;
    if(mouse_index<3u) return;
    mouse_index=0u;
    if(mouse_packet[0]&0xC0u) return;                    /* overflow: discard packet */
    mouse_buttons=(uint8_t)(mouse_packet[0]&7u);
    sand_input_event_t e={0};
    e.type=SAND_INPUT_POINTER; e.modifiers=modifiers;
    e.mouse_dx=(mouse_packet[0]&0x10u)?(int16_t)((int16_t)mouse_packet[1]-256):(int16_t)mouse_packet[1];
    e.mouse_dy=(mouse_packet[0]&0x20u)?(int16_t)((int16_t)mouse_packet[2]-256):(int16_t)mouse_packet[2];
    e.mouse_dy=(int16_t)-e.mouse_dy;                     /* PS/2 Y grows upward; screen Y grows downward */
    e.buttons=mouse_buttons;
    enqueue(&e);
}
static void drain(uint8_t want_mouse) {
    for(uint32_t guard=0;guard<16u;++guard) {
        uint8_t status=in_port(PS2_STATUS);
        if((status&1u)==0u) return;
        uint8_t data=in_port(PS2_DATA);
        if((status&0x20u)!=0u) { if(want_mouse) mouse_byte(data); } else if(!want_mouse) keyboard_byte(data);
    }
}
static void keyboard_irq(void) { drain(0u); }
static void mouse_irq(void) { drain(1u); }

int ps2_input_init(void) {
    q_head=q_tail=0u; dropped=0u; mouse_index=0u; mouse_buttons=0u; modifiers=0u; extended=0u; mouse_ok=0u;
    while(in_port(PS2_STATUS)&1u) (void)in_port(PS2_DATA);    /* flush stale bytes */
    if(controller_command(0xA8u)!=0) return -1;                 /* enable aux port */
    if(controller_command(0x20u)!=0 || wait_output_full()!=0) return -1;
    uint8_t config=in_port(PS2_DATA);
    config=(uint8_t)((config|0x03u)&~0x30u);                    /* IRQ1+IRQ12 on, both clocks on */
    if(controller_command(0x60u)!=0 || wait_input_clear()!=0) return -1;
    out_port(PS2_DATA,config);
    int keyboard_ok=(device_write(0xF4u,0)==0);
    mouse_ok=(uint8_t)(device_write(0xF4u,1)==0);
    (void)device_register("kbd0","keyboard","ps2-irq1",keyboard_ok?SAND_DEVICE_READY:SAND_DEVICE_FAILED);
    (void)device_register("mouse0","pointer","ps2-irq12",mouse_ok?SAND_DEVICE_READY:SAND_DEVICE_ABSENT);
    if(keyboard_ok && irq_install(1u,keyboard_irq)!=0) return -1;
    if(mouse_ok && irq_install(12u,mouse_irq)!=0) return -1;
    return keyboard_ok?0:-1;
}
int ps2_input_poll(sand_input_event_t *event) {
    if(event==0) return 0;
    uint32_t flags; __asm__ volatile("pushfl; popl %0; cli":"=r"(flags)::"memory");
    int have=(q_tail!=q_head);
    if(have) { *event=queue[q_tail]; q_tail=(q_tail+1u)%EVENT_QUEUE; }
    if(flags&0x200u) __asm__ volatile("sti":::"memory");
    return have;
}
uint32_t ps2_input_dropped(void) { return dropped; }
uint8_t ps2_input_modifiers(void) { return modifiers; }
int ps2_mouse_present(void) { return mouse_ok; }
