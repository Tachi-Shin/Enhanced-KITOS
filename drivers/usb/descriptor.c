#include <drivers/usb/usb.h>
#include <kernel/error.h>
static unsigned int le16(const uint8_t *p){return p[0]|((unsigned int)p[1]<<8);}
int usb_parse_device_prefix(const uint8_t *p,size_t size,enum usb_speed speed,uint16_t *ep0)
{
    if(!p||!ep0||size<8||p[0]!=18||p[1]!=1||speed>USB_SUPER)return -K_EINVAL;
    if(speed==USB_SUPER){if(p[7]!=9||le16(p+2)<0x300)return -K_EINVAL;*ep0=512;return 0;}
    if((p[7]!=8&&p[7]!=16&&p[7]!=32&&p[7]!=64)||(speed==USB_LOW&&p[7]!=8)||(speed==USB_HIGH&&p[7]!=64))return -K_EINVAL;
    *ep0=p[7];return 0;
}
int usb_parse_bos(const uint8_t *data,size_t size)
{
    if(!data||size<5||size>USB_CONFIG_MAX||data[0]!=5||data[1]!=15||le16(data+2)!=size||!data[4])return -K_EINVAL;
    unsigned int caps=0;bool super=false;
    for(size_t pos=5;pos<size;){
        unsigned int n=data[pos];if(n<3||n>size-pos||data[pos+1]!=16)return -K_EINVAL;
        if(data[pos+2]==3){
            if(super||n!=10||!(le16(data+pos+4)&8)||data[pos+6]>3||data[pos+7]>10||le16(data+pos+8)>2047)return -K_EINVAL;
            super=true;
        }
        ++caps;pos+=n;
    }
    return super&&caps==data[4]?0:-K_EINVAL;
}
static int parse_companion(const uint8_t *d,unsigned int n,unsigned int type,unsigned int mps,struct usb_endpoint *ep)
{
    if(n!=6||d[2]>15)return -K_EINVAL;
    unsigned int attr=d[3],bytes=le16(d+4),maximum=mps*(d[2]+1);
    if(type==2){if(mps!=1024||attr>16||bytes)return -K_EINVAL;}
    else if(type==3){if(attr||!bytes||bytes>maximum)return -K_EINVAL;}
    else if(type==1){if((attr&~3U)||(attr&3)==3||!bytes||bytes>maximum*((attr&3)+1))return -K_EINVAL;}
    else return -K_EINVAL;
    if(ep){ep->max_burst=d[2];ep->companion_attributes=d[3];ep->bytes_per_interval=(uint16_t)bytes;ep->companion=true;}
    return 0;
}
int usb_parse_config(const uint8_t *data,size_t length,struct usb_device *dev,uint8_t *configuration)
{
    if(!data||!dev||!configuration||dev->speed>USB_SUPER||length<9||length>USB_CONFIG_MAX||
       data[0]!=9||data[1]!=2||!data[5]||!(data[7]&0x80)||le16(data+2)!=length)return -K_EINVAL;
    struct usb_interface *current=NULL;struct usb_endpoint *last=NULL;
    unsigned int expected=0,count=0,pending_type=0,pending_mps=0;
    bool numbers[256]={0},endpoints[256]={0},need_companion=false,have_interface=false;
    dev->interface_count=0;
    for(size_t pos=9;pos<length;){
        unsigned int n=data[pos];if(n<2||n>length-pos)return -K_EINVAL;
        const uint8_t *d=data+pos;
        if(need_companion&&d[1]!=48)return -K_EINVAL;
        if(d[1]==48){
            if(dev->speed!=USB_SUPER||!need_companion||parse_companion(d,n,pending_type,pending_mps,last))return -K_EINVAL;
            need_companion=false;last=NULL;
        }else if(d[1]==4){
            if(n<9||(current&&current->endpoint_count!=expected))return -K_EINVAL;
            current=NULL;
            have_interface=true;
            if(d[3]==0){
                if(numbers[d[2]])return -K_EINVAL;
                numbers[d[2]]=true;
                if(count==USB_INTERFACES_MAX||d[4]>USB_ENDPOINTS_MAX)return -K_ENOSPC;
                current=&dev->interfaces[count++];
                *current=(struct usb_interface){.number=d[2],.class_code=d[5],.subclass=d[6],.protocol=d[7]};expected=d[4];
            }
        }else if(d[1]==5){
            if(n<7||!have_interface)return -K_EINVAL;
            unsigned int mps=le16(d+4),type=d[3]&3;
            if(dev->speed==USB_SUPER){
                if(!type||!mps||mps>1024||(d[2]&0x70)||!(d[2]&15)||
                   ((type==1||type==3)&&(!d[6]||d[6]>16))||(type==2&&d[6]))return -K_EINVAL;
                need_companion=true;pending_type=type;pending_mps=mps;last=NULL;
            }
            if(current){
                if(current->endpoint_count==expected||!(d[2]&15)||(d[2]&0x70)||!mps||mps>1024||!type||endpoints[d[2]])return -K_EINVAL;
                endpoints[d[2]]=true;last=&current->endpoints[current->endpoint_count++];
                *last=(struct usb_endpoint){.address=d[2],.type=(uint8_t)type,.interval=d[6],.max_packet=(uint16_t)mps};
            }
        }
        pos+=n;
    }
    if(need_companion||!count||count!=data[4]||(current&&current->endpoint_count!=expected))return -K_EINVAL;
    dev->interface_count=(uint8_t)count;*configuration=data[5];return 0;
}
int usb_hub_port_speed(enum usb_speed hub_speed,uint16_t status,enum usb_speed *speed)
{
    if(!speed)return -K_EINVAL;
    if(hub_speed==USB_SUPER){if(status&0x1C00)return -K_ENOTSUP;*speed=USB_SUPER;}
    else *speed=(status&0x200)?USB_LOW:(status&0x400)?USB_HIGH:USB_FULL;
    return 0;
}
