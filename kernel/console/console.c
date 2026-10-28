#include <kernel/console.h>
#include <kernel/error.h>
#include <drivers/usb/input.h>
#include <kernel/device.h>
#include <arch/irq.h>
#include <arch/io.h>
#include <kernel/printk.h>
#define OUTPUT_SIZE 65536U
static char output[OUTPUT_SIZE];
static uint32_t output_head,output_tail,dropped;
static bool lost;
static struct char_device *console;
static int input_pending;
static char input_byte;
void console_bind(struct char_device *device) { console = device; }
static int input_get(char *out)
{
    if (!console || !console->try_getc || !out) return -K_ENODEV;
    int rc = console->try_getc(console->context, out);
    return rc ? rc : usb_input_getc(out);
}
int console_try_getc(char *out)
{
    if(!out)return -K_EINVAL;
    uint64_t f=arch_irq_save();int rc=input_pending;
    if(rc){input_pending=0;if(rc>0)*out=input_byte;}else rc=input_get(out);
    arch_irq_restore(f);return rc;
}
bool console_input_ready(void)
{
    uint64_t f=arch_irq_save();if(!input_pending)input_pending=input_get(&input_byte);
    bool ready=input_pending!=0;arch_irq_restore(f);return ready;
}
void console_putc(char value)
{
    if(console&&console->putc){
        if(value=='\n')console->putc(console->context,'\r');
        console->putc(console->context,value);
    }
    uint64_t flags=arch_irq_save();
    if(output_head-output_tail==OUTPUT_SIZE){++output_tail;++dropped;lost=true;}
    output[output_head++%OUTPUT_SIZE]=value;arch_irq_restore(flags);
}
void console_write(const char *text)
{
    if (text) while (*text) console_putc(*text++);
}
void console_poll(void)
{
    const struct ek_video_ops *ops=device_backend(EK_DEVICE_VIDEO);if(!ops)return;
    console_graphics_poll();bool graphics=console_graphics_active();
    uint64_t flags=arch_irq_save();bool reset=lost;lost=false;arch_irq_restore(flags);
    if(reset&&!graphics){ops->write(27);ops->write('c');}
    for(unsigned int i=0;i<512;++i){
        flags=arch_irq_save();
        if(output_head==output_tail){arch_irq_restore(flags);break;}
        char c=output[output_tail++%OUTPUT_SIZE];arch_irq_restore(flags);if(!graphics)ops->write(c);
    }
    if(!graphics)ops->poll();
    mmio_dsb();
}
void console_info(struct kitos_terminal_info *out)
{
    *out=(struct kitos_terminal_info){.columns=80,.rows=25};
    uint64_t flags=arch_irq_save();
    const struct ek_video_ops *ops=device_backend(EK_DEVICE_VIDEO);if(ops)ops->info(out);
    out->dropped_bytes=dropped;
    arch_irq_restore(flags);
}
void console_print_display(void)
{
    struct kitos_terminal_info i;console_info(&i);
    if(!(i.flags&KITOS_DISPLAY_CONNECTED)){printk("display: disconnected; UART active\n");return;}
    printk("display: %ux%u %u-bpp %s pitch=%u terminal=%ux%u font=6x8 ASCII\n",i.width,i.height,i.depth,i.pixel_order?"RGB":"BGR",i.pitch,i.columns,i.rows);
    printk("display: double-buffer=%u virtual-offset readback; VSync=unverified\n",!!(i.flags&KITOS_DISPLAY_DOUBLE_BUFFER));
    if(i.refresh_millihz)printk("display: refresh=%u.%u%u%u Hz %s\n",i.refresh_millihz/1000,(i.refresh_millihz/100)%10,(i.refresh_millihz/10)%10,i.refresh_millihz%10,
        i.flags&KITOS_DISPLAY_TIMING_VERIFIED?"read back from firmware":"firmware/EDID");
    else printk("display: refresh=unknown (firmware did not expose timing)\n");
    printk("display: id=%u EDID=%u advertised-bpc=%u link=%s range=%s virtual=%u dropped=%u\n",i.display_id,
        !!(i.flags&KITOS_DISPLAY_EDID),i.advertised_bpc,!(i.flags&KITOS_DISPLAY_LINK_KNOWN)?"unknown":i.flags&KITOS_DISPLAY_DVI?"DVI":"HDMI",
        !(i.flags&KITOS_DISPLAY_LINK_KNOWN)?"unknown":i.flags&KITOS_DISPLAY_RGB_LIMITED?"limited":"full",!!(i.flags&KITOS_DISPLAY_VIRTUAL),i.dropped_bytes);
}
