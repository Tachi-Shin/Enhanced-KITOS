#include <kernel/console.h>
#include <kernel/device.h>
#include <kernel/ipc.h>
#include <kernel/error.h>
#include <arch/irq.h>
static uint32_t owner;
static volatile bool owner_gone,busy;
static unsigned int operation;
static uint32_t caller,source;
static struct ek_video_frame frame;
bool console_graphics_active(void){return owner!=0;}
void console_graphics_exit(uint32_t pid){if(owner==pid)owner_gone=true;}
/* Runs exclusively on the console worker. No reaper waits for this worker. */
void console_graphics_poll(void)
{
    if(owner_gone){
        const struct ek_video_ops *ops=device_backend(EK_DEVICE_VIDEO);
        owner=0;owner_gone=false;if(ops)ops->text_mode();
    }
}
static int read_frame(void *context,uint64_t offset,void *out,unsigned int n)
{(void)context;return ipc_shm_read(source,offset,out,n);}
static int job(void)
{
    console_graphics_poll();const struct ek_video_ops *ops=device_backend(EK_DEVICE_VIDEO);
    if(!ops)return -K_ENODEV;
    if(operation==KITOS_DISPLAY_ACQUIRE){
        if(owner)return -K_EBUSY;
        struct kitos_terminal_info info;ops->info(&info);
        if(!(info.flags&KITOS_DISPLAY_DOUBLE_BUFFER))return -K_ENOTSUP;
        owner=caller;return 0;
    }
    if(owner!=caller)return -K_EACCES;
    if(operation==KITOS_DISPLAY_RELEASE){owner=0;ops->text_mode();return 0;}
    return ops->frame(&frame);
}
int console_graphics(uint32_t pid,unsigned int op,uint32_t shm,unsigned int width,unsigned int height,unsigned int pitch)
{
    if(op<KITOS_DISPLAY_ACQUIRE||op>KITOS_DISPLAY_RELEASE)return -K_EINVAL;
    if(op==KITOS_DISPLAY_PRESENT){
        struct kitos_terminal_info info;console_info(&info);
        if(width!=info.width||height!=info.height||pitch<(uint64_t)width*4||
            (uint64_t)pitch*height>ipc_size(shm))return -K_EINVAL;
    }
    uint64_t f=arch_irq_save();if(busy){arch_irq_restore(f);return -K_EBUSY;}busy=true;arch_irq_restore(f);
    int rc=0;if(op==KITOS_DISPLAY_PRESENT)rc=ipc_retain(shm);
    if(!rc){caller=pid;operation=op;source=shm;frame=(struct ek_video_frame){NULL,read_frame,width,height,pitch};
        rc=console_work(job);if(op==KITOS_DISPLAY_PRESENT)ipc_close(shm);
    }
    f=arch_irq_save();busy=false;arch_irq_restore(f);return rc;
}
