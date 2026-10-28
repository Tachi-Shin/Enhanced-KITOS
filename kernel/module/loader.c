#include <kernel/module.h>
#include <ekitos/driver.h>
#include <kernel/error.h>
#include <kernel/clock.h>
#include <kernel/task.h>
#include <kernel/sync.h>
#include <kernel/printk.h>
#include <kernel/root.h>
#include <platform/platform.h>
#include <drivers/mailbox/mailbox.h>
#include <arch/memory.h>
#include <arch/io.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <kernel/device.h>
#include <drivers/gpio/gpio.h>
#include <drivers/uart/uart.h>
#include <arch/timer.h>
#include <kernel/irq.h>
#include <kernel/console.h>
#include <platform/irq.h>
#define DMA_PAGES_MAX 256U
struct loaded_module {
    unsigned int state; /* 0 free, 1 building, 2 loaded, 3 failed/pinned */
    uint64_t base;
    struct module_plan plan;
    uint64_t pages[MODULE_IMAGE_MAX/PAGE_SIZE], dma[DMA_PAGES_MAX];
    struct ek_driver_manifest manifest;
    struct ek_driver_api api;
    size_t framebuffer_bytes;
    struct usb_host *host;
    const struct usb_class_driver *classes[8];
    struct block_device *blocks[BLOCK_DEVICE_COUNT];
    const void *services[EK_DEVICE_COUNT];
    struct {unsigned int id;bool (*handler)(void *);void *context;} irqs[8];
};
static struct loaded_module modules[MODULE_COUNT_MAX];
static struct kernel_gate module_gate;
static struct loaded_module *framebuffer_owner;
static struct loaded_module *owner(void *p)
{
    for(unsigned int i=0;i<MODULE_COUNT_MAX;++i) if(p==&modules[i] && modules[i].state) return p;
    return NULL;
}
static bool pointer_valid(struct loaded_module *m,const void *pointer,size_t length,unsigned int perms)
{
    uint64_t address=(uintptr_t)pointer;
    if(address<m->base) return false;
    uint64_t off=address-m->base;
    for(unsigned int i=0;i<m->plan.count;++i) {
        const struct module_segment *s=&m->plan.segments[i];
        if(off>=s->address && off-s->address<=s->memory_size && length<=s->memory_size-(off-s->address) &&
           (s->permissions&perms)==perms) return true;
    }
    return false;
}
static void *dma_alloc(void *token,uint64_t *physical)
{
    struct loaded_module *m=owner(token); if(!m||!physical)return NULL;
    for(unsigned int i=0;i<DMA_PAGES_MAX;++i) if(!m->dma[i]) {
        uint64_t p=page_alloc(); if(!p)return NULL;
        /* The Pi 4 PCIe inbound window covers the low 1 GiB. */
        if(p>=0x40000000ULL){(void)page_free(p);return NULL;}
        m->dma[i]=p; *physical=p;
        void *v=arch_phys_to_virt(p); memset(v,0,PAGE_SIZE); return v;
    }
    return NULL;
}
static int dma_free(void *token,void *address)
{
    struct loaded_module *m=owner(token); if(!m)return -K_EINVAL;
    uint64_t p=arch_virt_to_phys(address);
    for(unsigned int i=0;i<DMA_PAGES_MAX;++i) if(m->dma[i]&&m->dma[i]==p) {
        int rc=page_free(p); if(!rc)m->dma[i]=0; return rc;
    }
    return -K_EINVAL;
}
static bool has_dma(struct loaded_module *m)
{ for(unsigned int i=0;i<DMA_PAGES_MAX;++i)if(m->dma[i])return true; return false; }
static void *framebuffer_map(void *token,uint64_t physical,size_t bytes)
{
    struct loaded_module *m=owner(token);
    if(!m||framebuffer_owner||!bytes||bytes>64U*1024*1024)return NULL;
    uint32_t t[5]={0x10006,8,0,0,0};
    if(mailbox_property_list(t,sizeof(t))||t[2]!=0x80000008U)return NULL;
    uint64_t base=t[3]&0x3fffffffU,size=t[4];
    size_t mapped=(bytes+PAGE_SIZE-1)&~(size_t)(PAGE_SIZE-1);
    if(physical<base||physical-base>=size||mapped>size-(physical-base))return NULL;
    void *v=arch_framebuffer_map(physical,bytes);
    if(v){framebuffer_owner=m;m->framebuffer_bytes=bytes;}return v;
}
static bool mmio_width_allowed(uint64_t address,unsigned int width)
{
    unsigned int count; const struct memory_range *ranges=platform_mmio_ranges(&count);
    if(address&(width-1))return false;
    for(unsigned int i=0;i<count;++i)
        if(address>=ranges[i].base && address-ranges[i].base<ranges[i].size && ranges[i].size-(address-ranges[i].base)>=width)return true;
    return false;
}
static uint32_t read32(uint64_t address) { return mmio_width_allowed(address,4)?mmio_read32((uintptr_t)address):0xFFFFFFFFU; }
static void write32(uint64_t address,uint32_t value) { if(mmio_width_allowed(address,4)){mmio_write32((uintptr_t)address,value);mmio_dsb();} }
static uint8_t read8(uint64_t address) { return mmio_width_allowed(address,1)?mmio_read8((uintptr_t)address):255; }
static uint16_t read16(uint64_t address) { return mmio_width_allowed(address,2)?mmio_read16((uintptr_t)address):65535; }
static void write8(uint64_t address,uint8_t value) { if(mmio_width_allowed(address,1)){mmio_write8((uintptr_t)address,value);mmio_dsb();} }
static void write16(uint64_t address,uint16_t value) { if(mmio_width_allowed(address,2)){mmio_write16((uintptr_t)address,value);mmio_dsb();} }
static void sleep_ms(uint32_t ms) { (void)task_sleep_ms(ms); }
static void dma_sync(void *p,size_t n,unsigned int direction) { (void)direction;arch_dma_sync(p,n); }
static void log_message(const char *message,uint64_t a,uint64_t b) { printk("[driver] %s a=%llx b=%llx\n",message,a,b); }
static int attach_host(void *token,struct usb_host *host)
{
    struct loaded_module *m=owner(token); if(!m||m->host||!pointer_valid(m,host,sizeof(*host),VM_READ|VM_WRITE))return -K_EINVAL;
    int rc=usb_attach_host(host); if(!rc)m->host=host; return rc;
}
static int detach_host(void *token,struct usb_host *host)
{
    struct loaded_module *m=owner(token); if(!m||m->host!=host)return -K_ENODEV;
    int rc=usb_detach_host(host); if(!rc)m->host=NULL; return rc;
}
static int register_class(void *token,const struct usb_class_driver *driver)
{
    struct loaded_module *m=owner(token); if(!m||!pointer_valid(m,driver,sizeof(*driver),VM_READ))return -K_EINVAL;
    for(unsigned int i=0;i<8;++i) if(!m->classes[i]) {
        int rc=usb_register_class(driver); if(!rc)m->classes[i]=driver; return rc;
    }
    return -K_ENOSPC;
}
static int unregister_class(void *token,const struct usb_class_driver *driver)
{
    struct loaded_module *m=owner(token); if(!m)return -K_EINVAL;
    for(unsigned int i=0;i<8;++i)if(m->classes[i]==driver) {
        int rc=usb_unregister_class(driver); if(!rc)m->classes[i]=NULL; return rc;
    }
    return -K_ENOENT;
}
static int register_block(void *token,struct block_device *device)
{
    struct loaded_module *m=owner(token);
    if(!m||!pointer_valid(m,device,sizeof(*device),VM_READ|VM_WRITE))return -K_EINVAL;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(m->blocks[i]==device)return 0;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(!m->blocks[i]){
        int rc=block_register(device);if(!rc)m->blocks[i]=device;return rc;
    }
    return -K_ENOSPC;
}
static int unregister_block(void *token,struct block_device *device)
{
    struct loaded_module *m=owner(token);if(!m)return -K_EINVAL;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(m->blocks[i]==device){
        int rc=block_unregister(device);if(!rc||rc==-K_ENOENT){m->blocks[i]=NULL;return 0;}return rc;
    }
    return -K_ENOENT;
}
static int register_irq(void *token,unsigned int id,bool (*handler)(void *),void *context,unsigned int priority)
{
    struct loaded_module *m=owner(token);
    if(!m||priority>15||!pointer_valid(m,(const void *)handler,4,VM_EXEC)||((uintptr_t)handler&3)||
       (context&&!pointer_valid(m,context,1,VM_READ)))return -K_EINVAL;
    for(unsigned int i=0;i<8;++i)if(!m->irqs[i].handler){
        int rc=irq_register(id,handler,context);if(rc)return rc;
        m->irqs[i].id=id;m->irqs[i].handler=handler;m->irqs[i].context=context;
        irq_controller_enable(id,priority);return 0;
    }
    return -K_ENOSPC;
}
static int unregister_irq(void *token,unsigned int id,bool (*handler)(void *),void *context)
{
    struct loaded_module *m=owner(token);if(!m)return -K_EINVAL;
    for(unsigned int i=0;i<8;++i)if(m->irqs[i].handler==handler&&m->irqs[i].id==id&&m->irqs[i].context==context){
        int rc=irq_unregister(id,handler,context);if(!rc)m->irqs[i].handler=NULL;return rc;
    }
    return -K_ENOENT;
}
static int cleanup_registrations(struct loaded_module *m)
{
    /* A published boot backend may already be referenced by another device
     * or the IRQ path. Retain even a failed init until a reboot. */
    for(unsigned int i=0;i<EK_DEVICE_COUNT;++i)if(m->services[i])return -K_EBUSY;
    if(m->host && detach_host(m,m->host))return -K_EBUSY;
    for(unsigned int i=0;i<8;++i)if(m->classes[i] && unregister_class(m,m->classes[i]))return -K_EBUSY;
    for(unsigned int i=0;i<BLOCK_DEVICE_COUNT;++i)if(m->blocks[i]&&unregister_block(m,m->blocks[i]))return -K_EBUSY;
    for(unsigned int i=0;i<8;++i)if(m->irqs[i].handler&&unregister_irq(m,m->irqs[i].id,m->irqs[i].handler,m->irqs[i].context))return -K_EBUSY;
    if(m->framebuffer_bytes){arch_framebuffer_unmap(m->framebuffer_bytes);m->framebuffer_bytes=0;framebuffer_owner=NULL;}
    return 0;
}
static int publish_device(void *token,enum ek_device_kind kind,const void *backend)
{
    static const unsigned int sizes[EK_DEVICE_COUNT]={sizeof(struct gpio_device),sizeof(struct uart_device),
        sizeof(struct ek_firmware_ops),sizeof(struct ek_irq_ops),sizeof(struct ek_timer_ops),sizeof(struct ek_video_ops)};
    struct loaded_module *m=owner(token);
    if(!m||kind>=EK_DEVICE_COUNT||m->services[kind]||!pointer_valid(m,backend,sizes[kind],VM_READ))return -K_EINVAL;
    int rc=device_publish(kind,backend);if(!rc)m->services[kind]=backend;return rc;
}
static int gpio_function(unsigned int pin,unsigned int function)
{ return gpio_set_function(pin,(enum gpio_function)function); }
static int gpio_pull(unsigned int pin,unsigned int pull)
{ return gpio_set_pull(pin,(enum gpio_pull)pull); }
static int read_image(void *file,uint64_t offset,void *buffer,uint32_t size)
{
    uint64_t position; uint32_t done; int rc;
    do { rc=vfs_seek(file,(int64_t)offset,0,&position);if(rc==-K_EBUSY)task_sleep_ms(1); } while(rc==-K_EBUSY);
    if(rc)return rc;
    do { rc=vfs_read(file,buffer,size,&done);if(rc==-K_EBUSY)task_sleep_ms(1); } while(rc==-K_EBUSY);
    return rc?rc:done==size?0:-K_ENOEXEC;
}
static int map_page(void *context,uint64_t offset,unsigned int perms,const uint8_t *bytes)
{
    struct loaded_module *m=context;
    if(offset>=MODULE_IMAGE_MAX || (offset&4095) || m->pages[offset/PAGE_SIZE])return -K_EINVAL;
    uint64_t p=page_alloc(); if(!p)return -K_ENOMEM;
    memcpy(arch_phys_to_virt(p),bytes,PAGE_SIZE);
    int rc=arch_module_map(m->base+offset,p,perms);
    if(rc){page_free(p);return rc;}
    m->pages[offset/PAGE_SIZE]=p; return 0;
}
static int relocate(void *context,uint64_t offset,uint64_t value)
{
    struct loaded_module *m=context;
    if(offset>=MODULE_IMAGE_MAX || (offset&7) || !m->pages[offset/PAGE_SIZE])return -K_EINVAL;
    memcpy((uint8_t *)arch_phys_to_virt(m->pages[offset/PAGE_SIZE])+offset%PAGE_SIZE,&value,8);return 0;
}
static void release_image(struct loaded_module *m)
{
    for(unsigned int i=0;i<MODULE_IMAGE_MAX/PAGE_SIZE;++i)if(m->pages[i]) {
        (void)arch_module_unmap(m->base+(uint64_t)i*PAGE_SIZE);(void)page_free(m->pages[i]);
    }
    *m=(struct loaded_module){0};
}
static int load_module(const char *path,const struct image_reader *memory)
{
    if(!memory&&(!path||*path!='/'))return -K_EINVAL;
    if(!kernel_gate_try(&module_gate))return -K_EBUSY;
    struct loaded_module *m=NULL;
    for(unsigned int i=0;i<MODULE_COUNT_MAX;++i)if(!modules[i].state){m=&modules[i];break;}
    int rc=-K_ENOSPC; struct file *file=NULL;
    if(!m)goto end;
    *m=(struct loaded_module){.state=1,.base=MODULE_VIRTUAL_BASE+(uint64_t)(m-modules)*MODULE_VIRTUAL_STRIDE};
    struct image_reader reader;
    if(memory)reader=*memory;
    else {
        do {rc=vfs_open(path,VFS_READ,&file);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);
        if(rc)goto fail;
        struct vfs_stat stat;do{rc=vfs_fstat(file,&stat);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);if(rc)goto fail;
        reader=(struct image_reader){file,stat.size,read_image};
    }
    rc=module_validate(&reader,&m->plan);if(rc)goto fail;
    const struct module_memory_ops ops={map_page,relocate};
    rc=module_image_load(&reader,&m->plan,m->base,&ops,m);if(rc)goto fail;
    const struct ek_driver_manifest *manifest=(const void *)(uintptr_t)(m->base+m->plan.entry);
    if(!pointer_valid(m,manifest,sizeof(*manifest),VM_READ)){rc=-K_ENOEXEC;goto fail;}
    m->manifest=*manifest;
    bool name_terminated=false;
    for(unsigned int i=0;i<sizeof(m->manifest.name);++i)if(!m->manifest.name[i])name_terminated=true;
    if(m->manifest.magic!=EK_DRIVER_MAGIC || m->manifest.abi!=EK_DRIVER_ABI || m->manifest.size!=sizeof(*manifest) ||
       !name_terminated || !m->manifest.name[0] ||
       !pointer_valid(m,(const void *)m->manifest.init,4,VM_EXEC) || ((uintptr_t)m->manifest.init&3) ||
       (m->manifest.fini && (!pointer_valid(m,(const void *)m->manifest.fini,4,VM_EXEC)||((uintptr_t)m->manifest.fini&3)))){rc=-K_ENOEXEC;goto fail;}
    for(unsigned int i=0;i<MODULE_COUNT_MAX;++i)if(&modules[i]!=m && modules[i].state && !strcmp(modules[i].manifest.name,m->manifest.name)){rc=-K_EEXIST;goto fail;}
    for(unsigned int i=0;i<MODULE_IMAGE_MAX/PAGE_SIZE;++i)if(m->pages[i])arch_sync_code(arch_phys_to_virt(m->pages[i]),PAGE_SIZE);
    m->api=(struct ek_driver_api){.abi=EK_DRIVER_ABI,.size=sizeof(m->api),.owner=m,.platform=platform_name(),
        .now_ms=clock_ms,.sleep_ms=sleep_ms,.log=log_message,.dma_alloc=dma_alloc,.dma_free=dma_free,.dma_sync=dma_sync,
        .read32=read32,.write32=write32,.firmware_xhci_reset=mailbox_notify_xhci_reset,
        .usb_attach_host=attach_host,.usb_detach_host=detach_host,.usb_register_class=register_class,.usb_unregister_class=unregister_class,
        .usb_control=usb_control,.usb_interrupt_in=usb_interrupt_in,.input_emit=usb_input_emit,
        .usb_bulk=usb_bulk,.usb_clear_halt=usb_clear_halt,.usb_begin=usb_begin,.usb_end=usb_end,
        .block_register=register_block,.block_unregister=unregister_block,
        .device_publish=publish_device,.counter_read=arch_counter_read,.counter_frequency=arch_counter_frequency,
        .delay_us=clock_delay_us,.read8=read8,.read16=read16,.write8=write8,.write16=write16,
        .gate_try=kernel_gate_try,.gate_release=kernel_gate_release,.uart_rx_push=uart_rx_push,.uart_rx_overrun=uart_rx_overrun,
        .gpio_function=gpio_function,.gpio_pull=gpio_pull,.firmware_clock=mailbox_clock_rate,
        .irq_register=register_irq,.irq_unregister=unregister_irq,
        .firmware_request=mailbox_property_list,.framebuffer_map=framebuffer_map};
    rc=m->manifest.init(&m->api);
    if(rc) {
        if(cleanup_registrations(m)||has_dma(m)){m->state=3;goto end;}
        goto fail;
    }
    m->state=2;printk("[module] loaded %s\n",m->manifest.name);goto end;
fail:
    if(m)release_image(m);
end:
    if(file){int close_rc;do{close_rc=vfs_close(file);if(close_rc==-K_EBUSY)task_sleep_ms(1);}while(close_rc==-K_EBUSY);}
    kernel_gate_release(&module_gate);return rc;
}
int module_load(const char *path) { return load_module(path,NULL); }
struct module_blob {const uint8_t *bytes;size_t size;};
static int memory_read(void *p,uint64_t offset,void *out,uint32_t size)
{
    struct module_blob *b=p;if(offset>b->size||size>b->size-offset)return -K_ENOEXEC;
    memcpy(out,b->bytes+offset,size);return 0;
}
int module_load_memory(const void *bytes,size_t size)
{
    if(!bytes||size>MODULE_FILE_MAX)return -K_EINVAL;
    struct module_blob b={bytes,size};struct image_reader r={&b,size,memory_read};return load_module(NULL,&r);
}
int module_unload(const char *name)
{
    if(!name)return -K_EINVAL;
    if(!kernel_gate_try(&module_gate))return -K_EBUSY;
    int rc=-K_ENOENT;
    for(unsigned int i=0;i<MODULE_COUNT_MAX;++i)if(modules[i].state && !strcmp(modules[i].manifest.name,name)) {
        struct loaded_module *m=&modules[i];
        for(unsigned int k=0;k<EK_DEVICE_COUNT;++k)if(m->services[k]){rc=-K_EBUSY;goto done;}
        rc=m->manifest.fini?m->manifest.fini():0;
        if(!rc)rc=cleanup_registrations(m);
        if(!rc && has_dma(m))rc=-K_EBUSY;
        if(!rc){printk("[module] unloaded %s\n",m->manifest.name);release_image(m);}break;
    }
done:
    kernel_gate_release(&module_gate);return rc;
}
void module_print(void)
{
    if(!kernel_gate_try(&module_gate)){printk("[module] busy; retry\n");return;}
    unsigned int count=0;
    for(unsigned int i=0;i<MODULE_COUNT_MAX;++i)if(modules[i].state){++count;printk("[module] %s state=%u base=%llx\n",modules[i].manifest.name,modules[i].state,modules[i].base);}
    printk("[module] count=%u ABI=%u\n",count,EK_DRIVER_ABI);kernel_gate_release(&module_gate);
}
void module_autoload(void *unused)
{
    (void)unused;
    root_layout_init();
    const char *names[]={"VIDEO","HID","STORAGE",!strcmp(platform_name(),"rpi4/qemu")?"DWC2":"PI4USB"};
    for(unsigned int i=0;i<4;++i){
        char path[64]="/lib/modules/";strcpy(path+strlen(path),names[i]);strcpy(path+strlen(path),".EKM");
        int rc=module_load(path);
        if(rc==-K_ENOENT||rc==-K_ENODEV){strcpy(path,"/DRIVERS/");strcpy(path+strlen(path),names[i]);strcpy(path+strlen(path),".EKM");rc=module_load(path);}
        if(rc){rc=module_embedded(names[i]);if(rc)printk("[module] %s.EKM: %d\n",names[i],rc);}
    }
    int id=task_create("console",console_worker,NULL);
    if(id>0)(void)task_protect((uint32_t)id);
    id=task_create("init",shell_init,NULL);
    if(id>0)(void)task_protect((uint32_t)id);
}
