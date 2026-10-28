#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/usb/xhci.h>
#include <kernel/error.h>
static unsigned int checks,opens,addresses,configured,depth_requests,power_requests,timeout_requests;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"SS line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static const uint8_t device_desc[]={18,1,0,3,9,0,3,9,0x34,0x12,0x78,0x56,0,1,0,0,0,1};
static const uint8_t bos[]={5,15,22,0,2, 7,16,2,2,0,0,0, 10,16,3,0,14,0,1,10,32,0};
static const uint8_t config[]={9,2,44,0,1,1,0,0x80,0, 9,4,0,0,2,8,6,0x50,0,
    7,5,0x81,2,0,4,0, 6,48,15,0,0,0, 7,5,2,2,0,4,0, 6,48,15,0,0,0};
static const uint8_t hub_config[]={9,2,31,0,1,1,0,0x80,0, 9,4,0,0,1,9,0,0,0,
    7,5,0x81,3,2,0,12, 6,48,0,0,2,0};
static int open_device(void *c,struct usb_device *d){(void)c;CHECK(d->speed==USB_SUPER&&d->ep0_size==512);d->host_id=1;++opens;return 0;}
static int address_device(void *c,struct usb_device *d,uint8_t a){(void)c;CHECK(d->ep0_size==512&&a==7);++addresses;return 0;}
static int configure(void *c,struct usb_device *d){(void)c;CHECK(d->address==7&&d->interfaces[0].endpoints[0].companion&&d->interfaces[0].endpoints[0].bytes_per_interval==2);++configured;return 0;}
static void sleep_ms(uint32_t ms){CHECK(ms==3||ms==20);}
static int request(struct usb_device *d,uint8_t type,uint8_t req,uint16_t value,uint16_t index,void *buf,uint16_t len,unsigned int *done)
{
    (void)d;*done=0;
    if(type==128&&req==6){
        const uint8_t *source=NULL;unsigned int size=0;
        if(value==0x100){source=device_desc;size=sizeof(device_desc);}
        if(value==0xf00){source=bos;size=sizeof(bos);}
        if(value==0x200){source=hub_config;size=sizeof(hub_config);}
        CHECK(source);if(size>len)size=len;memcpy(buf,source,size);*done=size;return 0;
    }
    if(type==0&&req==9){CHECK(value==1&&!len);return 0;}
    if(type==0xa0&&req==6){const uint8_t hub[]={12,0x2a,3,0,0,0,0,0,0,0,0,0};CHECK(value==0x2a00&&len==12);memcpy(buf,hub,12);*done=12;return 0;}
    if(type==0x20&&req==12){CHECK(value==d->depth&&index==0&&!len);++depth_requests;return 0;}
    CHECK(type==0x23&&req==3&&index>=1&&index<=3&&!len);
    if(value==8)++power_requests;else{CHECK(value==23||value==24);++timeout_requests;}
    return 0;
}
static void descriptor_tests(void)
{
    uint16_t mps;CHECK(!usb_parse_device_prefix(device_desc,8,USB_SUPER,&mps)&&mps==512);
    CHECK(usb_parse_device_prefix(device_desc,7,USB_SUPER,&mps)<0);
    uint8_t bad[18];memcpy(bad,device_desc,18);bad[7]=8;CHECK(usb_parse_device_prefix(bad,8,USB_SUPER,&mps)<0);
    bad[7]=9;bad[3]=2;CHECK(usb_parse_device_prefix(bad,8,USB_SUPER,&mps)<0);
    struct usb_device d={.speed=USB_SUPER};uint8_t selected;
    CHECK(!usb_parse_config(config,sizeof(config),&d,&selected));
    CHECK(d.interfaces[0].endpoints[0].companion&&d.interfaces[0].endpoints[0].max_packet==1024&&d.interfaces[0].endpoints[1].max_burst==15);
    uint8_t b[sizeof(config)];unsigned int fields[]={2,20,22,23,25,26,27,28,29,30,31,32,39,40,41,42,43};
    for(unsigned int n=0;n<sizeof(config);++n)CHECK(usb_parse_config(config,n,&d,&selected)<0);
    for(unsigned int i=0;i<sizeof(fields)/sizeof(fields[0]);++i){
        memcpy(b,config,sizeof(b));b[fields[i]]=0xff;CHECK(usb_parse_config(b,sizeof(b),&d,&selected)<0);
    }
    memcpy(b,config,sizeof(b));b[25]=6;b[26]=47;CHECK(usb_parse_config(b,sizeof(b),&d,&selected)<0); /* missing immediate companion */
    memcpy(b,config,sizeof(b));b[27]=0;b[40]=0;CHECK(!usb_parse_config(b,sizeof(b),&d,&selected)); /* zero-based burst */
    for(unsigned int i=0;i<sizeof(config);++i)for(unsigned int bit=0;bit<8;++bit){memcpy(b,config,sizeof(b));b[i]^=1U<<bit;CHECK(usb_parse_config(b,sizeof(b),&d,&selected)<=0);}
    CHECK(!usb_parse_bos(bos,sizeof(bos)));uint8_t bb[sizeof(bos)];
    for(unsigned int n=0;n<sizeof(bos);++n)CHECK(usb_parse_bos(bos,n)<0);
    for(unsigned int i=0;i<sizeof(bos);++i)for(unsigned int bit=0;bit<8;++bit){memcpy(bb,bos,sizeof(bb));bb[i]^=1U<<bit;CHECK(usb_parse_bos(bb,sizeof(bb))<=0);}
    enum usb_speed speed;CHECK(!usb_hub_port_speed(USB_SUPER,0x203,&speed)&&speed==USB_SUPER);
    CHECK(usb_hub_port_speed(USB_SUPER,0x603,&speed)==-K_ENOTSUP);
}
static void protocol_tests(void)
{
    uint32_t h[]={0x03000002,0x20425355,0x0401,7};struct xhci_protocol p;
    CHECK(!xhci_parse_protocol(h,NULL,&p)&&p.first==1&&p.count==4&&p.slot_type==7&&p.speed_ids[USB_SUPER]==4);
    h[0]=0x02000002;CHECK(!xhci_parse_protocol(h,NULL,&p)&&p.speed_ids[USB_HIGH]==3&&!p.speed_ids[USB_SUPER]);
    h[0]=0x03000002;h[2]|=2U<<28;
    uint32_t psi[]={ (5U<<16)|(3U<<4)|(1U<<8)|9U, (10U<<16)|(3U<<4)|(1U<<8)|(1U<<14)|10U };
    CHECK(!xhci_parse_protocol(h,psi,&p)&&p.speed_ids[USB_SUPER]==9); /* non-default PSIV */
    CHECK(xhci_parse_protocol(h,NULL,&p)==-K_EINVAL);
    psi[0]&=~15U;CHECK(xhci_parse_protocol(h,psi,&p)==-K_EINVAL);
    psi[0]=(5U<<16)|(3U<<4)|9U;psi[1]=psi[0];CHECK(xhci_parse_protocol(h,psi,&p)==-K_EINVAL);
    h[2]=0;CHECK(!xhci_parse_protocol(h,NULL,&p)&&!p.count); /* disabled protocol group */
    h[1]=0;CHECK(xhci_parse_protocol(h,NULL,&p)==-K_EINVAL);
}
int main(void)
{
    descriptor_tests();protocol_tests();
    const struct usb_host_ops ops={.open=open_device,.address=address_device,.configured=configure};struct usb_host host={.ops=&ops};
    struct usb_device d={.speed=USB_SUPER,.depth=2};bool hub=false;
    CHECK(!usb_enumerate_device(&host,&d,7,request,sleep_ms,&hub));CHECK(hub&&opens==1&&addresses==1&&configured==1);
    CHECK(!usb_configure_hub(&host,&d,request,sleep_ms));CHECK(d.hub_ports==3&&depth_requests==1&&power_requests==3&&timeout_requests==6&&configured==2);
    printf("SuperSpeed descriptors/BOS/companion/PSI/shared enumeration/hub control: %u checks PASS\n",checks);
}
