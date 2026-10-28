#include <drivers/usb/usb.h>
#include <kernel/clock.h>
#include <kernel/error.h>
#include <kernel/printk.h>
#include <kernel/task.h>
#include <kernel/sync.h>
#include <lib/string.h>
#include <kernel/storage.h>
static struct usb_host *host;
static struct usb_device devices[USB_DEVICES_MAX];
static const struct usb_class_driver *classes[8];
static struct kernel_gate gate;
static uint64_t next_scan;
static unsigned int failures;
static bool worker_created;
static const char *speed_name(enum usb_speed speed)
{
    switch(speed){case USB_LOW:return "Low";case USB_FULL:return "Full";
    case USB_HIGH:return "High";case USB_SUPER:return "SuperSpeed";default:return "Unknown";}
}
int usb_begin(struct usb_device *dev)
{
    if(!kernel_gate_try(&gate))return -K_EBUSY;
    bool found=false;
    for(unsigned int i=0;i<USB_DEVICES_MAX;++i)if(dev==&devices[i]&&devices[i].used)found=true;
    if(!host||!found){kernel_gate_release(&gate);return -K_ENODEV;}
    return 0;
}
void usb_end(void){kernel_gate_release(&gate);}

static int packet(struct usb_device *dev, struct usb_endpoint *ep, unsigned int pid,
                   void *buffer, unsigned int length, unsigned int *done, bool retry)
{
    uint64_t until = clock_ms() + 250;
    int rc;
    do {
        rc = host->ops->packet(host->context, dev, ep, pid, buffer, length, done);
        if (rc != -K_EAGAIN || !retry) return rc;
        task_sleep_ms(1);
    } while (clock_ms() < until);
    return -K_ETIMEDOUT;
}
int usb_control(struct usb_device *dev, uint8_t type, uint8_t request, uint16_t value,
                uint16_t index, void *buffer, uint16_t length, unsigned int *done)
{
    if (!host || !dev || !done || (length && !buffer) || length > USB_CONFIG_MAX) return -K_EINVAL;
    if (host->ops->control) return host->ops->control(host->context,dev,type,request,value,index,buffer,length,done);
    uint8_t setup[8] = {type, request, (uint8_t)value, (uint8_t)(value >> 8),
        (uint8_t)index, (uint8_t)(index >> 8), (uint8_t)length, (uint8_t)(length >> 8)};
    struct usb_endpoint ep = {.max_packet=dev->ep0_size};
    unsigned int n = 0; *done = 0;
    int rc = packet(dev, &ep, 3, setup, 8, &n, true);
    if (rc || n != 8) return rc ? rc : -K_EIO;
    ep.address = type & 0x80;
    unsigned int pid = 2;
    while (*done < length) {
        unsigned int chunk = length - *done;
        if (chunk > ep.max_packet) chunk = ep.max_packet;
        rc = packet(dev, &ep, pid, (uint8_t *)buffer + *done, chunk, &n, true);
        if (rc || n > chunk) return rc ? rc : -K_EIO;
        *done += n; pid ^= 2;
        if (n < chunk) { if (!(type & 0x80)) return -K_EIO; break; }
    }
    ep.address = length && !(type & 0x80) ? 0x80 : length ? 0 : 0x80;
    return packet(dev, &ep, 2, NULL, 0, &n, true);
}
int usb_interrupt_in(struct usb_device *dev, struct usb_endpoint *ep, void *buffer, unsigned int *done)
{
    if (!host || !dev || !ep || !buffer || !done || !(ep->address & 0x80) || ep->type != 3 || ep->max_packet > 64) return -K_EINVAL;
    int rc = packet(dev, ep, ep->toggle ? 2 : 0, buffer, ep->max_packet, done, false);
    if (!rc) ep->toggle ^= 1;
    return rc;
}
int usb_bulk(struct usb_device *dev,struct usb_endpoint *ep,void *buffer,unsigned int length,unsigned int *done)
{
    if(!host||!dev||!ep||!done||ep->type!=2||!ep->max_packet||ep->max_packet>(dev->speed==USB_SUPER?1024:512)||(length&&!buffer))return -K_EINVAL;
    *done=0;
    if(dev->speed==USB_SUPER&&!host->ops->bulk)return -K_ENOTSUP;
    if(host->ops->bulk)return host->ops->bulk(host->context,dev,ep,buffer,length,done);
    do{
        unsigned int n=length-*done;if(n>ep->max_packet)n=ep->max_packet;
        unsigned int actual=0;int rc=packet(dev,ep,ep->toggle?2:0,buffer?(uint8_t *)buffer+*done:NULL,n,&actual,true);
        if(rc)return rc;
        if(actual>n)return -K_EIO;
        ep->toggle^=1;*done+=actual;
        if(actual<n){if(!(ep->address&128))return -K_EIO;break;}
    }while(*done<length);
    return 0;
}
int usb_clear_halt(struct usb_device *dev,struct usb_endpoint *ep)
{
    unsigned int done;int rc=usb_control(dev,2,1,0,ep->address,NULL,0,&done);if(rc)return rc;
    if(host->ops->clear_halt)rc=host->ops->clear_halt(host->context,dev,ep);
    if(!rc)ep->toggle=0;
    return rc;
}
int usb_register_class(const struct usb_class_driver *driver)
{
    if (!driver || !driver->bind || !driver->poll || !driver->disconnect || !driver->name) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc=-K_ENOSPC;
    for (unsigned int i=0;i<8;++i) if(classes[i]==driver) { rc=0;break; }
    if(rc) for(unsigned int i=0;i<8;++i) if(!classes[i]) {classes[i]=driver;rc=0;break;}
    if (!rc) for (unsigned int i=0;i<USB_DEVICES_MAX;++i) if(devices[i].used)
        for(unsigned int j=0;j<devices[i].interface_count;++j) {
            struct usb_interface *iface=&devices[i].interfaces[j];
            if (!iface->driver && iface->class_code==driver->class_code && !driver->bind(&devices[i],iface)) iface->driver=driver;
        }
    kernel_gate_release(&gate); return rc;
}
int usb_unregister_class(const struct usb_class_driver *driver)
{
    if (!driver) return -K_EINVAL;
    if (!kernel_gate_try(&gate)) return -K_EBUSY;
    for (unsigned int i=0;i<USB_DEVICES_MAX;++i) if(devices[i].used)
        for(unsigned int j=0;j<devices[i].interface_count;++j) {
            struct usb_interface *iface=&devices[i].interfaces[j];
            if(iface->driver==driver) { driver->disconnect(&devices[i],iface); iface->driver=NULL; }
        }
    for(unsigned int i=0;i<8;++i) if(classes[i]==driver) classes[i]=NULL;
    kernel_gate_release(&gate); return 0;
}
static struct usb_device *at_port(uint8_t parent, uint8_t port)
{
    for (unsigned int i = 0; i < USB_DEVICES_MAX; ++i)
        if (devices[i].used && devices[i].parent == parent && devices[i].port == port) return &devices[i];
    return NULL;
}
static int detach(struct usb_device *dev)
{
    for (unsigned int i = 0; i < USB_DEVICES_MAX; ++i)
        if (devices[i].used && devices[i].parent == dev->address && detach(&devices[i])) return -K_EBUSY;
    for (unsigned int i = 0; i < dev->interface_count; ++i) {
        struct usb_interface *iface = &dev->interfaces[i];
        if (iface->driver) { iface->driver->disconnect(dev, iface); iface->driver=NULL; }
    }
    if (host->ops->release && host->ops->release(host->context,dev)) return -K_EBUSY;
    printk("[usb] disconnected address=%u\n", dev->address);
    *dev = (struct usb_device){0};
    return 0;
}
static void enumeration_sleep(uint32_t ms){(void)task_sleep_ms(ms);}
static int enumerate_device(struct usb_device *dev,uint8_t address)
{
    bool hub;int rc=usb_enumerate_device(host,dev,address,usb_control,enumeration_sleep,&hub);if(rc)return rc;
    for(unsigned int i=0;i<dev->interface_count;++i){
        struct usb_interface *iface=&dev->interfaces[i];
        for(unsigned int j=0;j<8;++j)if(classes[j]&&classes[j]->class_code==iface->class_code&&!classes[j]->bind(dev,iface)){iface->driver=classes[j];break;}
    }
    if(hub&&(rc=usb_configure_hub(host,dev,usb_control,enumeration_sleep)))return rc;
    dev->used=true;
    printk("[usb] configured address=%u parent=%u port=%u vid=%x pid=%x speed=%u(%s) interfaces=%u hub_ports=%u\n",
        address,dev->parent,dev->port,dev->vendor,dev->product,dev->speed,speed_name(dev->speed),dev->interface_count,dev->hub_ports);
    return 0;
}
static int enumerate(uint8_t parent, uint8_t port, enum usb_speed speed, uint8_t depth)
{
    if (depth>4) return -K_ENOTSUP;
    struct usb_device *dev=NULL;
    for(unsigned int i=0;i<USB_DEVICES_MAX;++i) if(!devices[i].used) {dev=&devices[i];break;}
    if(!dev) return -K_ENOSPC;
    *dev=(struct usb_device){.parent=parent,.port=port,.depth=depth,.speed=speed,.ep0_size=speed==USB_HIGH?64:8};
    int rc=enumerate_device(dev,(uint8_t)(dev-devices+1));
    if(rc) {
        for(unsigned int i=0;i<dev->interface_count;++i) if(dev->interfaces[i].driver)
            dev->interfaces[i].driver->disconnect(dev,&dev->interfaces[i]);
        if(host->ops->release && dev->host_id) (void)host->ops->release(host->context,dev);
        *dev=(struct usb_device){0};
    }
    return rc;
}
static int port_status(struct usb_device *hub, uint8_t port, uint8_t status[4])
{
    unsigned int done;
    int rc = usb_control(hub, 0xA3, 0, 0, port, status, 4, &done);
    return rc ? rc : done == 4 ? 0 : -K_EIO;
}
static void clear_hub_changes(struct usb_device *hub,uint8_t port,uint8_t changes)
{
    unsigned int done;
    if(hub->speed==USB_SUPER){
        static const uint8_t bits[]={0,3,4,5,6,7},features[]={16,19,20,29,25,26};
        for(unsigned int i=0;i<sizeof(bits);++i)if(changes&(1U<<bits[i]))(void)usb_control(hub,0x23,1,features[i],port,NULL,0,&done);
    }else for(unsigned int bit=0;bit<5;++bit)if(changes&(1U<<bit))(void)usb_control(hub,0x23,1,(uint16_t)(16+bit),port,NULL,0,&done);
}
static void scan_hub(struct usb_device *hub)
{
    for(uint8_t p=1;p<=hub->hub_ports;++p){
        uint8_t status[4];unsigned int done;
        if(port_status(hub,p,status))continue;
        bool super=hub->speed==USB_SUPER;
        unsigned int link=((unsigned int)status[0]|((unsigned int)status[1]<<8))>>5&15;
        bool warm=super&&(link==6||link==10);
        struct usb_device *child=at_port(hub->address,p);
        if(status[2]&(super?0x41:1))hub->failed_ports&=(uint16_t)~(1U<<(p-1));
        if(child&&(!(status[0]&1)||(status[2]&1)||warm)){if(detach(child))continue;child=NULL;}
        clear_hub_changes(hub,p,status[2]);
        if((!(status[0]&1)&&!warm)||(status[0]&8)||child||(hub->failed_ports&(1U<<(p-1))))continue;
        if(usb_control(hub,0x23,3,warm?28:4,p,NULL,0,&done))continue;
        task_sleep_ms(super?10:60);
        uint64_t end=clock_ms()+500;int rc;
        do{rc=port_status(hub,p,status);if(rc||(status[0]&0x13)==3)break;task_sleep_ms(10);}while(clock_ms()<end);
        if(rc||(status[0]&0x13)!=3){hub->failed_ports|=(uint16_t)(1U<<(p-1));continue;}
        enum usb_speed speed;
        rc=usb_hub_port_speed(hub->speed,(uint16_t)(status[0]|status[1]<<8),&speed);
        if(!rc)rc=!host->split_routes&&hub->speed==USB_HIGH&&speed!=USB_HIGH?-K_ENOTSUP:enumerate(hub->address,p,speed,(uint8_t)(hub->depth+1));
        if(rc){hub->failed_ports|=(uint16_t)(1U<<(p-1));++failures;printk("[usb] hub=%u port=%u enumeration=%d\n",hub->address,p,rc);}
        clear_hub_changes(hub,p,status[2]);
    }
}
int usb_init(struct usb_host *selected)
{
    if (host || !selected || !selected->ops || !selected->ops->start || !selected->ops->root_status ||
        !selected->ops->root_reset || !selected->ops->packet) return -K_ENODEV;
    int rc = selected->ops->start(selected->context);
    if (!rc) { host = selected; printk("[usb] host=%s\n", host->name); }
    return rc;
}
int usb_attach_host(struct usb_host *selected)
{
    if(!kernel_gate_try(&gate)) return -K_EBUSY;
    int rc=usb_init(selected);
    if(!rc && !worker_created) {
        int id=task_create("usb",usb_task,NULL);
        if(id<0) { if(selected->ops->stop)(void)selected->ops->stop(selected->context); host=NULL; rc=-K_EAGAIN; }
        else { (void)task_protect((uint32_t)id); worker_created=true; }
    }
    kernel_gate_release(&gate); return rc;
}
int usb_detach_host(struct usb_host *selected)
{
    if(!kernel_gate_try(&gate)) return -K_EBUSY;
    if(host!=selected) { kernel_gate_release(&gate); return -K_ENODEV; }
    for(unsigned int i=0;i<USB_DEVICES_MAX;++i) if(devices[i].used && detach(&devices[i])) {kernel_gate_release(&gate);return -K_EBUSY;}
    if(selected->ops->stop && selected->ops->stop(selected->context)) {kernel_gate_release(&gate);return -K_EBUSY;}
    host=NULL; kernel_gate_release(&gate); return 0;
}
void usb_task(void *unused)
{
    (void)unused;
    for (;;) {
        if (host && kernel_gate_try(&gate)) {
            uint64_t now = clock_ms();
            if (now >= next_scan) {
                next_scan = now + 100;
                for(unsigned int port=1;port<=(host->root_ports?host->root_ports:1);++port) {
                  bool connected, changed;
                  if (!host->ops->root_status(host->context, port, &connected, &changed)) {
                    struct usb_device *root = at_port(0, (uint8_t)port);
                    if (root && (!connected || changed)) { if(detach(root))continue; root = NULL; }
                    if (connected && !root) {
                        enum usb_speed speed;
                        task_sleep_ms(100);
                        int rc = host->ops->root_reset(host->context, port, &speed);
                        if (!rc) rc = enumerate(0, (uint8_t)port, speed, 0);
                        if (rc) { ++failures; next_scan = clock_ms() + 1000; printk("[usb] root enumeration=%d\n", rc); }
                    }
                  }
                }
                for (unsigned int i = 0; i < USB_DEVICES_MAX; ++i)
                    if (devices[i].used && devices[i].hub_ports) scan_hub(&devices[i]);
            }
            for (unsigned int i = 0; i < USB_DEVICES_MAX; ++i) if (devices[i].used)
                for (unsigned int j = 0; j < devices[i].interface_count; ++j) {
                    struct usb_interface *iface = &devices[i].interfaces[j];
                    if (iface->driver && now >= iface->next_poll) {
                        iface->driver->poll(&devices[i], iface);
                        iface->next_poll = clock_ms() + (iface->poll_interval_ms ? iface->poll_interval_ms : 10);
                    }
                }
            kernel_gate_release(&gate);
        }
        storage_poll();
        task_sleep_ms(1);
    }
}
void usb_print(void)
{
    if (!host) { printk("[usb] no supported host controller\n"); return; }
    if (!kernel_gate_try(&gate)) { printk("[usb] busy; retry\n"); return; }
    unsigned int count = 0;
    for (unsigned int i = 0; i < USB_DEVICES_MAX; ++i) if (devices[i].used) {
        struct usb_device *dev = &devices[i]; ++count;
        printk("[usb] address=%u parent=%u port=%u vid=%x pid=%x speed=%u(%s) hub_ports=%u\n",
            dev->address, dev->parent, dev->port, dev->vendor, dev->product, dev->speed,speed_name(dev->speed),dev->hub_ports);
        for (unsigned int j = 0; j < dev->interface_count; ++j) {
            struct usb_interface *iface = &dev->interfaces[j];
            printk("[usb] interface=%u class=%u protocol=%u driver=%s\n", iface->number, iface->class_code,
                iface->protocol, iface->driver ? iface->driver->name :
                iface->class_code == 9 && dev->hub_ports ? "hub-core" : "unbound");
        }
    }
    printk("[usb] devices=%u failures=%u\n", count, failures);
    kernel_gate_release(&gate);
}
