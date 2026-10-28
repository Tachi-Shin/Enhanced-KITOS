#include <ekitos/driver.h>
#include <lib/string.h>
static const struct ek_driver_api *api;
static unsigned int interrupts;
static bool handler(void *arg){++*(unsigned int *)arg;return false;}
static int init(const struct ek_driver_api *a)
{
    if(!a||a->abi!=EK_DRIVER_ABI||a->size<sizeof(*a))return -EK_EINVAL;
    if(strcmp(a->platform,"rpi4/qemu"))return -EK_ENODEV;
    api=a;
    /* Exercise the relocated MAILBOX.EKM and its SDK-owned DMA buffer against
     * QEMU's real property mailbox, including a second reuse of the buffer. */
    if(!a->firmware_clock(2,0)||a->firmware_clock(2,48000000)!=48000000||!a->firmware_clock(2,0))return -EK_EIO;
    a->log("Firmware module DMA/clock property",0,0);
    uint32_t tags[10]={0x40003,8,0,0,0,0x40003,8,0,0,0};
    if(a->firmware_request(tags,sizeof(tags))!=-EK_EINVAL||
        a->firmware_request(tags,16)!=-EK_EINVAL||
        a->firmware_request(tags,4096)!=-EK_EINVAL)return -EK_EIO;
    tags[2]=9;if(a->firmware_request(tags,20)!=-EK_EINVAL)return -EK_EIO;
    tags[2]=0;
    if(a->firmware_request(tags,20)||tags[2]!=0x80000008U||!tags[3]||!tags[4])return -EK_EIO;
    a->log("Firmware tag list/bounds/duplicate/reuse",tags[3],tags[4]);
    if(a->irq_register((void *)1,200,handler,&interrupts,8)!=-EK_EINVAL||
       a->irq_register(a->owner,200,NULL,NULL,8)!=-EK_EINVAL||
       a->irq_register(a->owner,200,handler,&interrupts,16)!=-EK_EINVAL||
       a->irq_register(a->owner,256,handler,&interrupts,8)!=-EK_EINVAL||
       a->irq_register(a->owner,30,handler,&interrupts,8)!=-EK_EBUSY)return -EK_EIO;
    int rc=a->irq_register(a->owner,200,handler,&interrupts,8);if(rc)return rc;
    if(a->irq_register(a->owner,200,handler,&interrupts,8)!=-EK_EBUSY)return -EK_EIO;
    a->log("IRQ ownership/bounds/duplicate",0,0);return 0;
}
static int fini(void){return api->irq_unregister(api->owner,200,handler,&interrupts);}
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest={
    EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"irq-test",init,fini};
