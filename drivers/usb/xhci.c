/* Polling xHCI 1.x backend. Controller-owned addresses, complete control TDs,
 * persistent interrupt requests, and bounded command/DMA lifetimes. */
#include <drivers/usb/xhci.h>
#include <lib/string.h>
#define PAGE 4096U
#define SLOTS USB_DEVICES_MAX
#define PORTS 32U
#define TYPE(n) ((uint32_t)(n)<<10)
struct trb { uint64_t parameter; uint32_t status,control; };
struct dma { void *v; uint64_t bus; };
struct ring { struct dma mem; unsigned int index,cycle; };
struct endpoint {
    struct ring ring;struct dma buffer;
    uint64_t token,data_token;unsigned int requested,actual,cc;
    bool pending,complete;
};
struct slot {
    struct dma input,output;
    struct endpoint ep[32];
    uint8_t logical,root,depth,speed,tt_slot,tt_port;
    uint32_t route;
    enum usb_speed usb_speed;
};
struct xhci {
    const struct ek_driver_api *a;uint64_t bar,offset,op,rt,db;
    unsigned int context_size,max_slots,ports,event_index,event_cycle;
    bool running,poisoned,dma64;
    struct ring commands;
    struct dma dcbaa,event,erst,scratch_array,scratch[32];
    unsigned int scratch_count;
    uint8_t port_major[PORTS+1],port_slot[PORTS+1],speed_ids[PORTS+1][4];
    uint64_t command_token;unsigned int command_cc,command_slot;
    struct slot slots[SLOTS+1];
    struct usb_host host;
};
static struct xhci controller;
static uint32_t rd(struct xhci *x,uint64_t r){return x->a->read32(x->bar+r);}
static void wr(struct xhci *x,uint64_t r,uint32_t v){x->a->write32(x->bar+r,v);}
static void wr64(struct xhci *x,uint64_t r,uint64_t v){wr(x,r,(uint32_t)v);wr(x,r+4,v>>32);}
static void sync_to(struct xhci *x,struct dma *d){x->a->dma_sync(d->v,PAGE,EK_DMA_TO_DEVICE);}
static void sync_from(struct xhci *x,struct dma *d){x->a->dma_sync(d->v,PAGE,EK_DMA_FROM_DEVICE);}
static int alloc(struct xhci *x,struct dma *d)
{
    uint64_t physical=0;d->v=x->a->dma_alloc(x->a->owner,&physical);
    if(!d->v)return -EK_ENOMEM;
    if((physical&4095) || physical>~0ULL-x->offset ||
       (!x->dma64 && (physical+x->offset)>0xFFFFF000ULL)){
        (void)x->a->dma_free(x->a->owner,d->v);*d=(struct dma){0};return -EK_ENOTSUP;
    }
    d->bus=physical+x->offset;memset(d->v,0,PAGE);sync_to(x,d);return 0;
}
static void free_dma(struct xhci *x,struct dma *d)
{if(d->v){(void)x->a->dma_free(x->a->owner,d->v);*d=(struct dma){0};}}
static int ring_alloc(struct xhci *x,struct ring *r)
{
    int rc=alloc(x,&r->mem);if(rc)return rc;r->index=0;r->cycle=1;
    struct trb *t=r->mem.v;t[255]=(struct trb){r->mem.bus,0,TYPE(6)|2|1};sync_to(x,&r->mem);return 0;
}
static uint64_t enqueue(struct xhci *x,struct ring *r,uint64_t parameter,uint32_t status,uint32_t control,bool more,bool held)
{
    struct trb *t=r->mem.v;unsigned int i=r->index;
    /* Invalidate ownership while writing the payload; publish cycle last.
     * Also works with coherent DMA, where stores are visible before doorbell. */
    t[i]=(struct trb){parameter,status,control|(r->cycle^1U)};
    sync_to(x,&r->mem);
    if(!held){t[i].control=control|r->cycle;sync_to(x,&r->mem);}
    uint64_t token=r->mem.bus+i*16;
    if(++r->index==255){t[255].control=TYPE(6)|2|r->cycle|(more?16U:0U);sync_to(x,&r->mem);r->index=0;r->cycle^=1;}
    return token;
}
static uint64_t push(struct xhci *x,struct ring *r,uint64_t parameter,uint32_t status,uint32_t control)
{return enqueue(x,r,parameter,status,control,false,false);}
static void events(struct xhci *x)
{
    if(!x->event.v)return;
    sync_from(x,&x->event);struct trb *t=x->event.v;unsigned int count=0;
    while(count++<256){
        struct trb e=t[x->event_index];if((e.control&1)!=x->event_cycle)break;
        unsigned int type=(e.control>>10)&63,cc=e.status>>24,slot=e.control>>24,ep=(e.control>>16)&31;
        if(type==33 && e.parameter==x->command_token){x->command_cc=cc;x->command_slot=slot;}
        if(type==32 && slot && slot<=x->max_slots && ep){
            struct endpoint *p=&x->slots[slot].ep[ep];
            if(p->pending){
                if(e.parameter==p->data_token && (cc==1||cc==13)){
                    unsigned int residual=e.status&0xFFFFFF;
                    p->actual=residual<=p->requested?p->requested-residual:0;
                }
                if(e.parameter==p->token || (cc!=1 && cc!=13)){
                    if(e.parameter==p->token && ep!=1){unsigned int residual=e.status&0xFFFFFF;p->actual=residual<=p->requested?p->requested-residual:0;}
                    p->cc=cc;p->complete=true;
                }
            }
        }
        if(++x->event_index==256){x->event_index=0;x->event_cycle^=1;}
    }
    wr64(x,x->rt+0x38,x->event.bus+x->event_index*16+8);
}
static int wait_mask(struct xhci *x,uint64_t r,uint32_t mask,uint32_t value,unsigned int ms)
{
    uint64_t end=x->a->now_ms()+ms;
    do{if((rd(x,r)&mask)==value)return 0;x->a->sleep_ms(1);}while(x->a->now_ms()<end);
    return -EK_ETIMEDOUT;
}
static int halt(struct xhci *x)
{wr(x,x->op,0);int rc=wait_mask(x,x->op+4,1,1,500);if(!rc)x->running=false;return rc;}
static int poison(struct xhci *x)
{x->poisoned=true;(void)halt(x);return -EK_ETIMEDOUT;}
static int command(struct xhci *x,uint64_t parameter,uint32_t status,uint32_t control,unsigned int *slot)
{
    if(!x->running||x->poisoned)return -EK_ENODEV;
    events(x);x->command_cc=0;x->command_token=push(x,&x->commands,parameter,status,control);wr(x,x->db,0);
    uint64_t end=x->a->now_ms()+1000;
    do{events(x);if(x->command_cc){if(slot)*slot=x->command_slot;return x->command_cc==1?0:-EK_EIO;}x->a->sleep_ms(1);}while(x->a->now_ms()<end);
    return poison(x);
}
static void free_slot(struct xhci *x,struct slot *s)
{
    for(unsigned int i=1;i<32;++i){free_dma(x,&s->ep[i].ring.mem);free_dma(x,&s->ep[i].buffer);}
    free_dma(x,&s->input);free_dma(x,&s->output);memset(s,0,sizeof(*s));
}
static int stop(void *context)
{
    struct xhci *x=context;
    if(x->running||x->poisoned){int rc=halt(x);if(rc)return rc;}
    for(unsigned int i=1;i<=SLOTS;++i)free_slot(x,&x->slots[i]);
    for(unsigned int i=0;i<32;++i)free_dma(x,&x->scratch[i]);
    free_dma(x,&x->scratch_array);free_dma(x,&x->dcbaa);free_dma(x,&x->event);free_dma(x,&x->erst);free_dma(x,&x->commands.mem);
    x->poisoned=false;return 0;
}
static int start(void *context)
{
    struct xhci *x=context;int rc=-EK_ENOTSUP;
    uint32_t cap=rd(x,0),hcs=rd(x,4),hcs2=rd(x,8),hcc=rd(x,16);
    x->op=cap&255;x->ports=hcs>>24;x->max_slots=hcs&255;if(x->max_slots>SLOTS)x->max_slots=SLOTS;
    x->context_size=(hcc&4)?64:32;x->rt=rd(x,0x18)&~31U;x->db=rd(x,0x14)&~3U;
    x->dma64=(hcc&1)!=0;
    x->scratch_count=((hcs2>>27)&31)|(((hcs2>>21)&31)<<5);
    if(x->op<0x20 || (x->op&3) || (cap>>16)<0x100 || !x->max_slots || !x->ports || x->ports>PORTS ||
       x->scratch_count>32 || x->op+0x400+x->ports*16>0x10000 || x->rt+0x40>0x10000 || x->db+256*4>0x10000)return rc;
    unsigned int ext=(hcc>>16)*4;bool protocols=false;
    for(unsigned int n=0;ext;++n){
        if(n>=64||ext<0x20||ext>0xfff8)return -EK_EINVAL;
        uint32_t v=rd(x,ext);
        if((v&255)==1){wr(x,ext,v|(1U<<24));rc=wait_mask(x,ext,1U<<16,0,1000);if(rc)return rc;wr(x,ext+4,0);}
        if((v&255)==2){
            if(ext>0xfff0)return -EK_EINVAL;
            uint32_t h[4]={v,rd(x,ext+4),rd(x,ext+8),rd(x,ext+12)},psi[15];
            unsigned int count=h[2]>>28;if(ext+16+count*4>0x10000)return -EK_EINVAL;
            for(unsigned int i=0;i<count;++i)psi[i]=rd(x,ext+16+i*4);
            struct xhci_protocol protocol;rc=xhci_parse_protocol(h,psi,&protocol);
            if(rc&&rc!=-EK_ENOTSUP)return rc;
            if(!rc&&protocol.count){
                if((unsigned int)protocol.first+protocol.count-1>x->ports)return -EK_EINVAL;
                protocols=true;
                for(unsigned int p=protocol.first;p<(unsigned int)protocol.first+protocol.count;++p){
                    if(x->port_major[p])return -EK_EINVAL;
                    x->port_major[p]=protocol.major;x->port_slot[p]=protocol.slot_type;memcpy(x->speed_ids[p],protocol.speed_ids,4);
                }
            }
        }
        unsigned int next=((v>>8)&255)*4;if(!next)break;ext+=next;
    }
    if(!protocols)for(unsigned int p=1;p<=x->ports;++p){x->port_major[p]=2;x->speed_ids[p][USB_LOW]=2;x->speed_ids[p][USB_FULL]=1;x->speed_ids[p][USB_HIGH]=3;}
    if((rc=wait_mask(x,x->op+4,1U<<11,0,1000)) || (rc=halt(x)))return rc;
    wr(x,x->op,2);
    if((rc=wait_mask(x,x->op,2,0,1000)) || (rc=wait_mask(x,x->op+4,1U<<11,0,1000)))return rc;
    if(!(rd(x,x->op+8)&1))return -EK_ENOTSUP;
    if((rc=alloc(x,&x->dcbaa)) || (rc=ring_alloc(x,&x->commands)) || (rc=alloc(x,&x->event)) || (rc=alloc(x,&x->erst)))goto fail;
    if(x->scratch_count){
        if((rc=alloc(x,&x->scratch_array)))goto fail;
        for(unsigned int i=0;i<x->scratch_count;++i){if((rc=alloc(x,&x->scratch[i])))goto fail;((uint64_t *)x->scratch_array.v)[i]=x->scratch[i].bus;}
        sync_to(x,&x->scratch_array);((uint64_t *)x->dcbaa.v)[0]=x->scratch_array.bus;sync_to(x,&x->dcbaa);
    }
    uint64_t *erst=x->erst.v;erst[0]=x->event.bus;erst[1]=256;sync_to(x,&x->erst);
    x->event_index=0;x->event_cycle=1;
    wr64(x,x->op+0x30,x->dcbaa.bus);wr64(x,x->op+0x18,x->commands.mem.bus|1);wr(x,x->op+0x38,x->max_slots);
    wr(x,x->rt+0x20,0);wr(x,x->rt+0x24,0);wr(x,x->rt+0x28,1);wr64(x,x->rt+0x30,x->erst.bus);wr64(x,x->rt+0x38,x->event.bus);
    wr(x,x->op,1);x->running=true;x->poisoned=false;
    if((rc=wait_mask(x,x->op+4,1,0,500)))goto fail;
    for(unsigned int p=1;p<=x->ports;++p){
        uint64_t port=x->op+0x400+(p-1)*16;
        if(x->port_major[p]==3)wr(x,port+4,rd(x,port+4)&~0x1FFFFU); /* U1/U2 and forced LPM off */
        wr(x,port,1U<<9);
    }
    x->host.root_ports=x->ports;x->a->log("xHCI started",cap,x->ports);return 0;
fail:
    if(stop(x))x->poisoned=true;
    return rc;
}
static bool bad_link(struct xhci *x,unsigned int port,uint32_t status)
{unsigned int link=(status>>5)&15;return x->port_major[port]==3&&(link==6||link==10||(status&(1U<<24)));}
static int root_status(void *context,unsigned int port,bool *connected,bool *changed)
{
    struct xhci *x=context;if(!x->running||x->poisoned)return -EK_ENODEV;
    if(!port||port>x->ports)return -EK_EINVAL;
    if(!x->port_major[port])return -EK_ENOTSUP;
    events(x);
    uint64_t r=x->op+0x400+(port-1)*16;uint32_t v=rd(x,r);bool recover=bad_link(x,port,v);
    *connected=(v&1)!=0||recover;*changed=(v&(1U<<17))!=0||recover;
    if(v&0xFE0000)wr(x,r,(v&(1U<<9))|(v&0xFE0000));
    return 0;
}
static int root_reset(void *context,unsigned int port,enum usb_speed *speed)
{
    struct xhci *x=context;if(!port||port>x->ports||!speed)return -EK_EINVAL;
    if(!x->running||x->poisoned)return -EK_ENODEV;
    uint64_t r=x->op+0x400+(port-1)*16;uint32_t v=rd(x,r);
    bool recover=bad_link(x,port,v);
    if(!(v&1)&&!recover)return -EK_ENODEV;
    bool warm=recover||(x->port_major[port]==3&&!(v&2));
    uint32_t reset=warm?(1U<<31):16;
    wr(x,r,(v&(1U<<9))|reset);int rc=wait_mask(x,r,reset|16|3,3,500);if(rc)return rc;
    unsigned int id=(rd(x,r)>>10)&15;
    for(unsigned int s=0;s<=USB_SUPER;++s)if(id&&x->speed_ids[port][s]==id){*speed=(enum usb_speed)s;return 0;}
    return -EK_ENOTSUP;
}
static uint32_t *input_slot(struct xhci *x,struct slot *s){return (void *)((uint8_t *)s->input.v+x->context_size);}
static uint32_t *input_ep(struct xhci *x,struct slot *s,unsigned int ep){return (void *)((uint8_t *)s->input.v+(ep+1)*x->context_size);}
static void copy_slot(struct xhci *x,struct slot *s)
{sync_from(x,&s->output);memset(s->input.v,0,PAGE);memcpy(input_slot(x,s),s->output.v,x->context_size);input_slot(x,s)[3]=0;}
static int release(void *context,struct usb_device *dev)
{
    struct xhci *x=context;unsigned int id=dev->host_id;if(!id)return 0;
    if(id>x->max_slots)return -EK_EINVAL;
    if(x->poisoned){if(x->running)return -EK_EBUSY;}else{int rc=command(x,0,0,TYPE(10)|(id<<24),NULL);if(rc)return rc;}
    ((uint64_t *)x->dcbaa.v)[id]=0;sync_to(x,&x->dcbaa);free_slot(x,&x->slots[id]);dev->host_id=0;return 0;
}
static int open_device(void *context,struct usb_device *dev)
{
    struct xhci *x=context;struct slot *parent=NULL;
    unsigned int root=dev->port;
    if(dev->parent){
        for(unsigned int i=1;i<=x->max_slots;++i)if(x->slots[i].logical==dev->parent){parent=&x->slots[i];break;}
        if(!parent||!dev->depth||dev->depth>5)return -EK_ENOTSUP;
        root=parent->root;
    }
    if(!root||root>x->ports||dev->speed>USB_SUPER||!x->speed_ids[root][dev->speed])return -EK_ENOTSUP;
    unsigned int id=0;int rc=command(x,0,0,TYPE(9)|((uint32_t)x->port_slot[root]<<16),&id);
    if(rc)return rc;
    if(!id||id>x->max_slots)return poison(x);
    dev->host_id=id;struct slot *s=&x->slots[id];s->root=(uint8_t)root;
    s->speed=x->speed_ids[root][dev->speed];s->usb_speed=dev->speed;s->depth=dev->depth;
    if(parent){
        s->route=parent->route|((uint32_t)(dev->port>15?15:dev->port)<<(4*(dev->depth-1)));
        if(dev->speed<=USB_FULL){if(parent->usb_speed==USB_HIGH){s->tt_slot=(uint8_t)(parent-x->slots);s->tt_port=dev->port;}else{s->tt_slot=parent->tt_slot;s->tt_port=parent->tt_port;}}
    }
    if((rc=alloc(x,&s->input)) || (rc=alloc(x,&s->output)) || (rc=ring_alloc(x,&s->ep[1].ring)) || (rc=alloc(x,&s->ep[1].buffer)))goto fail;
    ((uint32_t *)s->input.v)[1]=3;
    uint32_t *c=input_slot(x,s);c[0]=s->route|(s->speed<<20)|(1U<<27);c[1]=(uint32_t)s->root<<16;c[2]=s->tt_slot|((uint32_t)s->tt_port<<8);
    c=input_ep(x,s,1);c[1]=(4U<<3)|(3U<<1)|((dev->speed==USB_SUPER?512U:dev->speed==USB_HIGH?64U:8U)<<16);c[2]=(uint32_t)s->ep[1].ring.mem.bus|1;c[3]=s->ep[1].ring.mem.bus>>32;c[4]=8;
    ((uint64_t *)x->dcbaa.v)[id]=s->output.bus;sync_to(x,&x->dcbaa);sync_to(x,&s->input);
    rc=command(x,s->input.bus,0,TYPE(11)|(id<<24),NULL);if(!rc)return 0;
fail:
    if(release(x,dev))x->poisoned=true;
    return rc;
}
static int address_device(void *context,struct usb_device *dev,uint8_t address)
{
    struct xhci *x=context;struct slot *s=&x->slots[dev->host_id];copy_slot(x,s);
    memcpy(input_ep(x,s,1),(uint8_t *)s->output.v+x->context_size,x->context_size);
    ((uint32_t *)s->input.v)[1]=2;uint32_t *c=input_ep(x,s,1);c[0]&=~7U;c[1]=(c[1]&65535)|((uint32_t)dev->ep0_size<<16);sync_to(x,&s->input);
    int rc=command(x,s->input.bus,0,TYPE(13)|((uint32_t)dev->host_id<<24),NULL);if(!rc)s->logical=address;return rc;
}
static int configured(void *context,struct usb_device *dev)
{
    struct xhci *x=context;struct slot *s=&x->slots[dev->host_id];copy_slot(x,s);unsigned int add=1,max=1;int rc;
    for(unsigned int i=0;i<dev->interface_count;++i)for(unsigned int j=0;j<dev->interfaces[i].endpoint_count;++j){
        struct usb_endpoint *e=&dev->interfaces[i].endpoints[j];if(e->type!=2&&(e->type!=3||!(e->address&128)))continue;
        unsigned int id=(e->address&15)*2+((e->address&128)?1U:0U);if(id<=1||id>=32||!e->max_packet||e->max_packet>1024)return -EK_ENOTSUP;
        if((dev->speed==USB_LOW&&e->max_packet>8)||(dev->speed==USB_FULL&&e->max_packet>64))return -EK_ENOTSUP;
        if(e->type==2&&(dev->speed==USB_LOW||e->max_packet>(dev->speed==USB_SUPER?1024:512)))return -EK_ENOTSUP;
        if(dev->speed==USB_SUPER&&(!e->companion||e->max_burst>15||(e->type==2&&e->max_packet!=1024)))return -EK_EINVAL;
        if(id>max)max=id;
        struct endpoint *p=&s->ep[id];if(p->ring.mem.v)continue;
        if((rc=ring_alloc(x,&p->ring)) || (rc=alloc(x,&p->buffer)))return rc;
        unsigned int interval=0;if(e->type==2){}
        else if(dev->speed>=USB_HIGH){if(!e->interval||e->interval>16)return -EK_EINVAL;interval=e->interval-1;}
        else{unsigned int v=e->interval?e->interval:1;while((1U<<(interval+1))<=v)++interval;interval+=3;}
        uint32_t *c=input_ep(x,s,id);c[0]=interval<<16;c[1]=((e->type==2?((e->address&128)?6U:2U):7U)<<3)|(3U<<1)|((uint32_t)e->max_packet<<16);
        if(dev->speed==USB_SUPER)c[1]|=(uint32_t)e->max_burst<<8;
        c[2]=(uint32_t)p->ring.mem.bus|1;c[3]=p->ring.mem.bus>>32;
        uint32_t payload=e->type==3?(dev->speed==USB_SUPER?e->bytes_per_interval:e->max_packet):0;
        c[4]=e->max_packet|(payload<<16);add|=1U<<id;
    }
    uint32_t *c=input_slot(x,s);c[0]=(c[0]&~(31U<<27))|(max<<27);
    if(dev->hub_ports){c[0]|=1U<<26;if(dev->speed!=USB_SUPER&&dev->device_protocol==2)c[0]|=1U<<25;c[1]=(c[1]&0xFFFFFF)|((uint32_t)dev->hub_ports<<24);c[2]=(c[2]&~(3U<<16))|((uint32_t)dev->tt_think_time<<16);}
    ((uint32_t *)s->input.v)[1]=add;sync_to(x,&s->input);
    return command(x,s->input.bus,0,TYPE(add==1?13:12)|((uint32_t)dev->host_id<<24),NULL);
}
static int recover_endpoint(struct xhci *x,unsigned int slot,unsigned int id)
{
    struct endpoint *p=&x->slots[slot].ep[id];int rc=command(x,0,0,TYPE(14)|(id<<16)|(slot<<24),NULL);if(rc)return rc;
    /* The failed TD is no longer reused. Resume at the current producer. */
    uint64_t next=p->ring.mem.bus+p->ring.index*16+p->ring.cycle;
    return command(x,next,0,TYPE(16)|(id<<16)|(slot<<24),NULL);
}
/* Disconnect aborts the caller, but pending TDs and their DMA remain owned
 * by the host until the USB core completes Disable Slot during detach. */
static bool root_connected(struct xhci *x,const struct usb_device *dev)
{
    unsigned int root=x->slots[dev->host_id].root;
    return root&&root<=x->ports&&(rd(x,x->op+0x400+(root-1)*16)&1);
}
static int control(void *context,struct usb_device *dev,uint8_t type,uint8_t request,uint16_t value,uint16_t index,void *buffer,uint16_t length,unsigned int *done)
{
    struct xhci *x=context;if(!x->running||x->poisoned||!dev->host_id||dev->host_id>x->max_slots)return -EK_ENODEV;
    if(!root_connected(x,dev))return -EK_ENODEV;
    if(length>USB_CONFIG_MAX || (length&&!buffer))return -EK_EINVAL;
    struct endpoint *p=&x->slots[dev->host_id].ep[1];if(p->pending)return -EK_EBUSY;
    bool in=(type&128)!=0;uint64_t setup=type|((uint64_t)request<<8)|((uint64_t)value<<16)|((uint64_t)index<<32)|((uint64_t)length<<48);
    if(length&&!in){memcpy(p->buffer.v,buffer,length);sync_to(x,&p->buffer);}
    p->pending=true;p->complete=false;p->requested=length;p->actual=length;p->data_token=0;
    unsigned int first=p->ring.index;
    enqueue(x,&p->ring,setup,8,TYPE(2)|(1U<<6)|((length?(in?3U:2U):0U)<<16),true,true);
    if(length)p->data_token=enqueue(x,&p->ring,p->buffer.bus,length,TYPE(3)|(in?((1U<<16)|(1U<<2)):0),true,false);
    p->token=push(x,&p->ring,0,0,TYPE(4)|(1U<<5)|((!length||!in)?(1U<<16):0));
    /* Give the entire control TD to hardware only after the status stage exists. */
    ((struct trb *)p->ring.mem.v)[first].control^=1;sync_to(x,&p->ring.mem);
    wr(x,x->db+dev->host_id*4,1);
    uint64_t end=x->a->now_ms()+1000;
    do{events(x);if(p->complete)break;if(!root_connected(x,dev))return -EK_ENODEV;x->a->sleep_ms(1);}while(x->a->now_ms()<end);
    if(!p->complete)return poison(x);
    p->pending=false;if(p->cc!=1&&p->cc!=13){if(!root_connected(x,dev))return -EK_ENODEV;if(p->cc==6)(void)recover_endpoint(x,dev->host_id,1);return -EK_EIO;}
    if(length&&in){sync_from(x,&p->buffer);memcpy(buffer,p->buffer.v,p->actual);}if(done)*done=p->actual;return 0;
}
static int packet(void *context,const struct usb_device *dev,const struct usb_endpoint *ep,unsigned int pid,void *buffer,unsigned int length,unsigned int *done)
{
    struct xhci *x=context;(void)pid;
    if(!x->running||x->poisoned||!dev->host_id||dev->host_id>x->max_slots)return -EK_ENODEV;
    if(!root_connected(x,dev))return -EK_ENODEV;
    if(!(ep->address&128)||ep->type!=3||length>PAGE)return -EK_ENOTSUP;
    unsigned int id=(ep->address&15)*2+1;if(id<=1||id>=32)return -EK_EINVAL;
    struct endpoint *p=&x->slots[dev->host_id].ep[id];if(!p->ring.mem.v)return -EK_ENODEV;
    events(x);
    if(p->pending){
        if(!p->complete)return -EK_EAGAIN;
        p->pending=false;if(p->cc!=1&&p->cc!=13){if(p->cc==6)(void)recover_endpoint(x,dev->host_id,id);return -EK_EIO;}
        if(p->actual>length)return -EK_EOVERFLOW;
        sync_from(x,&p->buffer);memcpy(buffer,p->buffer.v,p->actual);*done=p->actual;return 0;
    }
    p->pending=true;p->complete=false;p->requested=length;p->actual=length;p->data_token=0;
    p->token=push(x,&p->ring,p->buffer.bus,length,TYPE(1)|(1U<<5)|(1U<<2));wr(x,x->db+dev->host_id*4,id);return -EK_EAGAIN;
}
static int clear_halt(void *context,struct usb_device *dev,struct usb_endpoint *ep)
{
    struct xhci *x=context;if(!dev->host_id||dev->host_id>x->max_slots)return -EK_ENODEV;
    unsigned int id=(ep->address&15)*2+((ep->address&128)?1U:0U);
    struct slot *s=&x->slots[dev->host_id];struct endpoint *p=&s->ep[id];
    if(!p->ring.mem.v||p->pending)return -EK_EBUSY;
    sync_from(x,&s->output);uint32_t *c=(void *)((uint8_t *)s->output.v+id*x->context_size);
    unsigned int state=c[0]&7;int rc=0;
    if(state==1)rc=command(x,0,0,TYPE(15)|(id<<16)|((uint32_t)dev->host_id<<24),NULL);
    else if(state==2)rc=command(x,0,0,TYPE(14)|(id<<16)|((uint32_t)dev->host_id<<24),NULL);
    else if(state!=3)return -EK_ENODEV;
    if(rc)return rc;
    uint64_t next=p->ring.mem.bus+p->ring.index*16+p->ring.cycle;
    if(state!=2){
        /* Reset Endpoint only accepts Halted. A stopped/idle pipe needs
         * Drop+Add to reset its USB 3 sequence number (USB 2 data toggle). */
        copy_slot(x,s);c=input_ep(x,s,id);
        memcpy(c,(uint8_t *)s->output.v+id*x->context_size,x->context_size);c[0]&=~7U;
        c[2]=(uint32_t)next;c[3]=next>>32;
        ((uint32_t *)s->input.v)[0]=1U<<id;((uint32_t *)s->input.v)[1]=1U|(1U<<id);sync_to(x,&s->input);
        return command(x,s->input.bus,0,TYPE(12)|((uint32_t)dev->host_id<<24),NULL);
    }
    return command(x,next,0,TYPE(16)|(id<<16)|((uint32_t)dev->host_id<<24),NULL);
}
static int bulk(void *context,struct usb_device *dev,struct usb_endpoint *ep,void *buffer,unsigned int length,unsigned int *done)
{
    struct xhci *x=context;*done=0;
    if(!x->running||x->poisoned||!dev->host_id||dev->host_id>x->max_slots)return -EK_ENODEV;
    if(!root_connected(x,dev))return -EK_ENODEV;
    unsigned int id=(ep->address&15)*2+((ep->address&128)?1U:0U);
    if(id<=1||id>=32||ep->type!=2)return -EK_EINVAL;
    struct endpoint *p=&x->slots[dev->host_id].ep[id];if(!p->ring.mem.v)return -EK_ENODEV;
    if(p->pending)return -EK_EBUSY;
    do{
        unsigned int n=length-*done;if(n>PAGE)n=PAGE;
        if(!(ep->address&128)&&n){memcpy(p->buffer.v,(uint8_t *)buffer+*done,n);sync_to(x,&p->buffer);}
        p->pending=true;p->complete=false;p->requested=n;p->actual=n;p->data_token=0;
        p->token=push(x,&p->ring,p->buffer.bus,n,TYPE(1)|(1U<<5)|((ep->address&128)?4U:0U));
        wr(x,x->db+dev->host_id*4,id);
        uint64_t end=x->a->now_ms()+3000;
        do{events(x);if(p->complete)break;if(!root_connected(x,dev))return -EK_ENODEV;x->a->sleep_ms(1);}while(x->a->now_ms()<end);
        if(!p->complete)return poison(x);
        p->pending=false;if(p->cc!=1&&p->cc!=13){if(!root_connected(x,dev))return -EK_ENODEV;return p->cc==6?-EK_EPIPE:-EK_EIO;}
        if(p->actual>n)return -EK_EIO;
        if((ep->address&128)&&p->actual){sync_from(x,&p->buffer);memcpy((uint8_t *)buffer+*done,p->buffer.v,p->actual);}
        *done+=p->actual;if(p->actual<n)break;
    }while(*done<length);
    return 0;
}
static const struct usb_host_ops ops={.start=start,.root_status=root_status,.root_reset=root_reset,.packet=packet,.open=open_device,.address=address_device,.configured=configured,.release=release,.control=control,.stop=stop,.bulk=bulk,.clear_halt=clear_halt};
struct usb_host *xhci_host(const struct ek_driver_api *a,uint64_t bar,uint64_t offset)
{
    if(controller.running||controller.dcbaa.v)return NULL;
    memset(&controller,0,sizeof(controller));controller.a=a;controller.bar=bar;controller.offset=offset;
    controller.host=(struct usb_host){.name="VL805/xHCI",.ops=&ops,.context=&controller,.split_routes=true};return &controller.host;
}
