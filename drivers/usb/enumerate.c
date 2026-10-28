/* Host-independent standard requests shared by EKITOS and the native xHCI
 * integration harness. Class probes and device-pool lifetime stay in core.c. */
#include <drivers/usb/usb.h>
#include <kernel/error.h>
static unsigned int le16(const uint8_t *p){return p[0]|((unsigned int)p[1]<<8);}
static int descriptor(usb_control_fn control,struct usb_device *dev,uint8_t type,void *p,uint16_t size,unsigned int *done)
{return control(dev,128,6,(uint16_t)(type<<8),0,p,size,done);}
int usb_enumerate_device(struct usb_host *host,struct usb_device *dev,uint8_t address,usb_control_fn control,void (*sleep_ms)(uint32_t),bool *hub)
{
    if(!host||!host->ops||!dev||!control||!sleep_ms||!hub||!address||address>127||dev->speed>USB_SUPER)return -K_EINVAL;
    dev->ep0_size=dev->speed==USB_SUPER?512:dev->speed==USB_HIGH?64:8;
    uint8_t data[USB_CONFIG_MAX];unsigned int done=0;int rc;
    if(host->ops->open&&(rc=host->ops->open(host->context,dev)))return rc;
    rc=descriptor(control,dev,1,data,8,&done);if(rc)return rc;
    uint16_t mps;if(done!=8||(rc=usb_parse_device_prefix(data,done,dev->speed,&mps)))return -K_EINVAL;
    dev->ep0_size=mps;
    rc=host->ops->address?host->ops->address(host->context,dev,address):control(dev,0,5,address,0,NULL,0,&done);
    if(rc)return rc;
    sleep_ms(3);dev->address=address;
    rc=descriptor(control,dev,1,data,18,&done);if(rc)return rc;
    if(done!=18||!data[17]||usb_parse_device_prefix(data,done,dev->speed,&mps)||mps!=dev->ep0_size)return -K_EINVAL;
    *hub=data[4]==9;dev->device_protocol=data[6];
    dev->vendor=(uint16_t)le16(data+8);dev->product=(uint16_t)le16(data+10);
    if(dev->speed==USB_SUPER){
        rc=descriptor(control,dev,15,data,5,&done);if(rc)return rc;
        if(done!=5||data[0]!=5||data[1]!=15||le16(data+2)<5||le16(data+2)>USB_CONFIG_MAX)return -K_EINVAL;
        uint16_t total=(uint16_t)le16(data+2);rc=descriptor(control,dev,15,data,total,&done);if(rc)return rc;
        if(done!=total||(rc=usb_parse_bos(data,total)))return -K_EINVAL;
    }
    rc=descriptor(control,dev,2,data,9,&done);if(rc)return rc;
    if(done!=9)return -K_EINVAL;
    uint16_t total=(uint16_t)le16(data+2);if(total<9||total>USB_CONFIG_MAX)return -K_ENOTSUP;
    rc=descriptor(control,dev,2,data,total,&done);if(rc)return rc;
    if(done!=total)return -K_EIO;
    uint8_t configuration;rc=usb_parse_config(data,total,dev,&configuration);if(rc)return rc;
    rc=control(dev,0,9,configuration,0,NULL,0,&done);if(rc)return rc;
    for(unsigned int i=0;i<dev->interface_count;++i)if(dev->interfaces[i].class_code==9)*hub=true;
    return host->ops->configured?host->ops->configured(host->context,dev):0;
}
int usb_configure_hub(struct usb_host *host,struct usb_device *dev,usb_control_fn control,void (*sleep_ms)(uint32_t))
{
    if(!host||!host->ops||!dev||!control||!sleep_ms)return -K_EINVAL;
    bool super=dev->speed==USB_SUPER;uint8_t data[12];unsigned int done;
    uint8_t type=super?0x2a:0x29;uint16_t size=super?12:9;
    int rc=control(dev,0xa0,6,(uint16_t)(type<<8),0,data,size,&done);if(rc)return rc;
    if(done<(super?12U:7U)||data[1]!=type||!data[2]||data[2]>(super?15:16)||data[0]<(super?12:7))return -K_ENOTSUP;
    dev->hub_ports=data[2];dev->tt_think_time=super?0:(data[3]>>5)&3;
    if(super){rc=control(dev,0x20,12,dev->depth,0,NULL,0,&done);if(rc)return rc;}
    if(host->ops->configured&&(rc=host->ops->configured(host->context,dev)))return rc;
    for(unsigned int p=1;p<=dev->hub_ports;++p){
        rc=control(dev,0x23,3,8,(uint16_t)p,NULL,0,&done);if(rc)return rc;
        if(super){
            /* LPM is not enabled: keep both downstream U1/U2 timeouts zero. */
            rc=control(dev,0x23,3,23,(uint16_t)p,NULL,0,&done);if(rc)return rc;
            rc=control(dev,0x23,3,24,(uint16_t)p,NULL,0,&done);if(rc)return rc;
        }
    }
    sleep_ms((uint32_t)data[5]*2+20);return 0;
}
