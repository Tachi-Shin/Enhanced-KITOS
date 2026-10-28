/* xHCI Supported Protocol and PSI decoding; no fixed port-number or Slot Type
 * assumptions. Support the USB 2 rates and USB 3 Gen1 x1 (5 Gbit/s). */
#include <drivers/usb/xhci.h>
int xhci_parse_protocol(const uint32_t h[4],const uint32_t *psi,struct xhci_protocol *out)
{
    if(!h||!out||(h[0]&255)!=2||h[1]!=0x20425355U)return -EK_EINVAL;
    unsigned int major=h[0]>>24,count=h[2]>>28;
    if(major!=2&&major!=3)return -EK_ENOTSUP;
    if((count&&!psi)||(((h[2]>>8)&255)&&!(h[2]&255)))return -EK_EINVAL;
    *out=(struct xhci_protocol){.first=h[2]&255,.count=(h[2]>>8)&255,.major=(uint8_t)major,.slot_type=h[3]&31};
    if(!count){
        if(major==2){out->speed_ids[USB_LOW]=2;out->speed_ids[USB_FULL]=1;out->speed_ids[USB_HIGH]=3;}
        else out->speed_ids[USB_SUPER]=4;
        return 0;
    }
    unsigned int seen=0;
    for(unsigned int i=0;i<count;++i){
        uint32_t p=psi[i];unsigned int id=p&15,kind=(p>>6)&3;
        if(!id||kind==1)return -EK_EINVAL;
        if(kind)continue; /* asymmetric/SSP rates are not enabled */
        if(seen&(1U<<id))return -EK_EINVAL;
        seen|=1U<<id;
        uint64_t rate=p>>16;for(unsigned int e=0;e<((p>>4)&3);++e)rate*=1000;
        int speed=-1;
        if(major==2){if(rate==1500000)speed=USB_LOW;else if(rate==12000000)speed=USB_FULL;else if(rate==480000000)speed=USB_HIGH;}
        else if(rate==5000000000ULL&&!(p&(3U<<14)))speed=USB_SUPER;
        if(speed>=0&&!out->speed_ids[speed])out->speed_ids[speed]=(uint8_t)id;
    }
    return 0;
}
