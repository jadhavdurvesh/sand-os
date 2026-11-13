#include "sand/console/console.h"
#include "sand/kernel/scheduler.h"

#define INPUT_CAPACITY 256u
static sand_console_sink sinks[SAND_CONSOLE_SINK_MAX];
static uint32_t sink_count;
static uint8_t input_ring[INPUT_CAPACITY];
static volatile uint32_t input_head,input_tail;
static char *capture_buffer;
static uint32_t capture_capacity,capture_used;
static int capture_active;
static char input_wait_token;

static uint32_t irq_save(void) { uint32_t f; __asm__ volatile("pushfl; popl %0; cli":"=r"(f)::"memory"); return f; }
static void irq_restore(uint32_t f) { if((f&0x200u)!=0u) __asm__ volatile("sti":::"memory"); }

int console_add_sink(sand_console_sink sink) {
    if(sink==0 || sink_count>=SAND_CONSOLE_SINK_MAX) return -1;
    for(uint32_t i=0;i<sink_count;++i) if(sinks[i]==sink) return 0;
    sinks[sink_count++]=sink; return 0;
}
void console_write(const char *data,uint32_t length) {
    if(data==0 || length==0u) return;
    uint32_t flags=irq_save();
    if(capture_active) {
        for(uint32_t i=0;i<length && capture_used+1u<capture_capacity;++i) capture_buffer[capture_used++]=data[i];
        capture_buffer[capture_used]='\0';
    }
    for(uint32_t i=0;i<sink_count;++i) sinks[i](data,length);
    irq_restore(flags);
}
void console_puts(const char *text) {
    uint32_t n=0u; while(text[n]) ++n;
    console_write(text,n);
}
void console_input_push(uint8_t byte) {
    uint32_t flags=irq_save();
    uint32_t next=(input_head+1u)%INPUT_CAPACITY;
    if(next!=input_tail) { input_ring[input_head]=byte; input_head=next; }
    irq_restore(flags);
    (void)scheduler_wake_all(&input_wait_token);
}
uint32_t console_input_read(uint8_t *destination,uint32_t capacity) {
    uint32_t flags=irq_save(), count=0u;
    while(count<capacity && input_tail!=input_head) {
        destination[count++]=input_ring[input_tail];
        input_tail=(input_tail+1u)%INPUT_CAPACITY;
    }
    irq_restore(flags); return count;
}
uint32_t console_input_pending(void) { return (input_head+INPUT_CAPACITY-input_tail)%INPUT_CAPACITY; }
const void *console_input_wait_object(void) { return &input_wait_token; }
void console_input_flush(void) { uint32_t f=irq_save(); input_tail=input_head; irq_restore(f); }

void console_capture_start(char *buffer,uint32_t capacity) {
    uint32_t f=irq_save();
    capture_buffer=buffer; capture_capacity=capacity; capture_used=0u;
    if(buffer && capacity) buffer[0]='\0';
    capture_active=(buffer!=0 && capacity>1u);
    irq_restore(f);
}
void console_capture_stop(void) { uint32_t f=irq_save(); capture_active=0; irq_restore(f); }
uint32_t console_capture_length(void) { return capture_used; }
int console_capture_contains(const char *needle) {
    if(capture_buffer==0 || needle==0 || needle[0]=='\0') return 0;
    for(uint32_t i=0;i<capture_used;++i) {
        uint32_t j=0u;
        while(needle[j] && i+j<capture_used && capture_buffer[i+j]==needle[j]) ++j;
        if(needle[j]=='\0') return 1;
    }
    return 0;
}
