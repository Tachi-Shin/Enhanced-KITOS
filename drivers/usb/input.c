#include <drivers/usb/input.h>
#include <arch/irq.h>
#include <kernel/error.h>
#include <kernel/printk.h>
#include <kernel/clock.h>
#include <kernel/ipc.h>
#define EVENTS 64U
#define CHARS 128U
static struct input_event events[EVENTS];
static unsigned int event_head, event_tail, char_head, char_tail;
static char chars[CHARS];
static uint64_t dropped_events, dropped_chars;
void usb_input_emit(const struct input_event *e, void *unused)
{
    (void)unused;
    uint64_t flags = arch_irq_save();
    if (event_head - event_tail < EVENTS) {
        struct input_event *stored = &events[event_head++ % EVENTS];
        *stored = *e; stored->time_ms = clock_ms();
    }
    else ++dropped_events;
    char c = e->type == 1 && e->value ? usb_key_ascii(e->code, e->modifiers, e->caps_lock) : 0;
    const char *sequence=NULL;
    if(e->type==1&&e->value){
        switch(e->code){
        case 0x4f:sequence="\033[C";break;case 0x50:sequence="\033[D";break;
        case 0x51:sequence="\033[B";break;case 0x52:sequence="\033[A";break;
        case 0x4a:sequence="\033[H";break;case 0x4d:sequence="\033[F";break;
        case 0x49:sequence="\033[2~";break;case 0x4c:sequence="\033[3~";break;
        case 0x4b:sequence="\033[5~";break;case 0x4e:sequence="\033[6~";break;
        }
    }
    unsigned int n=0;if(sequence)while(sequence[n])++n;else if(c)n=1;
    if(n){
        if(n<=CHARS-(char_head-char_tail))for(unsigned int i=0;i<n;++i)chars[char_head++%CHARS]=sequence?sequence[i]:c;
        else dropped_chars+=n;
        ipc_notify();
    }
    arch_irq_restore(flags);
}
int usb_input_getc(char *out)
{
    if (!out) return -K_EINVAL;
    uint64_t flags = arch_irq_save();
    int rc = 0;
    if (char_head != char_tail) { *out = chars[char_tail++ % CHARS]; rc = 1; }
    arch_irq_restore(flags);
    return rc;
}
unsigned int usb_input_read(struct kitos_input_event *out, unsigned int capacity)
{
    uint64_t flags = arch_irq_save();
    unsigned int count = 0;
    while (count < capacity && event_tail != event_head) {
        struct input_event e = events[event_tail++ % EVENTS];
        out[count++] = (struct kitos_input_event){.time_ms=e.time_ms, .device=e.device,
            .type=e.type, .code=e.code, .modifiers=e.modifiers, .value=e.value,
            .x=e.x, .y=e.y, .wheel=e.wheel, .flags=e.caps_lock ? KITOS_INPUT_CAPS_LOCK : 0};
    }
    arch_irq_restore(flags);
    return count;
}
void usb_input_print(void)
{
    for (;;) {
        uint64_t flags = arch_irq_save();
        if (event_tail == event_head) { arch_irq_restore(flags); break; }
        struct input_event e = events[event_tail++ % EVENTS];
        arch_irq_restore(flags);
        if (e.type == 1) printk("[input] dev=%u key=%u value=%d\n", e.device, e.code, e.value);
        else printk("[input] dev=%u mouse buttons=%u dx=%d dy=%d wheel=%d\n", e.device, e.code, e.x, e.y, e.wheel);
    }
    printk("[input] dropped_events=%llu dropped_chars=%llu\n", dropped_events, dropped_chars);
}
