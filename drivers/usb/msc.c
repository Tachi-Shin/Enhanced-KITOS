/* USB Mass Storage Bulk-Only Transport / SCSI transparent command set.
 * All command/data/status transactions are serialized by the USB core gate. */
#include <drivers/usb/msc.h>
#include <lib/string.h>
#define UNITS 8U
#define CONTEXTS 8U
#define COMMAND_FAILED (-200)
struct unit {
    struct usb_device *dev;struct usb_interface *iface;
    struct usb_endpoint *in,*out;
    struct block_device block;
    uint32_t tag;uint8_t lun;
    bool used,online,exposed;
};
struct context {struct usb_device *dev;struct usb_interface *iface;uint8_t max_lun;bool used;};
static struct unit units[UNITS];
static struct context contexts[CONTEXTS];
static const struct ek_driver_api *api;
static uint32_t le32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void putle32(uint8_t *p,uint32_t n){for(unsigned int i=0;i<4;++i)p[i]=(uint8_t)(n>>(8*i));}
static uint32_t be32(const uint8_t *p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static uint64_t be64(const uint8_t *p){return ((uint64_t)be32(p)<<32)|be32(p+4);}
static void putbe32(uint8_t *p,uint32_t n){for(unsigned int i=0;i<4;++i)p[i]=(uint8_t)(n>>(24-8*i));}
static void putbe64(uint8_t *p,uint64_t n){putbe32(p,n>>32);putbe32(p+4,(uint32_t)n);}
static void offline(struct unit *u){u->online=false;u->block.offline=true;}
static int recovery(struct unit *u)
{
    unsigned int done;int rc=api->usb_control(u->dev,0x21,255,0,u->iface->number,NULL,0,&done);
    int a=api->usb_clear_halt(u->dev,u->in),b=api->usb_clear_halt(u->dev,u->out);
    if(rc||a||b){offline(u);return -EK_EIO;}
    return 0;
}
static int transport(struct unit *u,const uint8_t *cdb,unsigned int cdb_len,bool in,void *data,unsigned int length,unsigned int *actual)
{
    uint8_t cbw[31]={0},csw[13];unsigned int done=0,transferred=0;int rc;
    if(!u->online)return -EK_ENODEV;
    uint32_t tag=++u->tag;if(!tag)tag=++u->tag;
    putle32(cbw,0x43425355);putle32(cbw+4,tag);putle32(cbw+8,length);
    cbw[12]=in?128:0;cbw[13]=u->lun;cbw[14]=(uint8_t)cdb_len;memcpy(cbw+15,cdb,cdb_len);
    rc=api->usb_bulk(u->dev,u->out,cbw,sizeof(cbw),&done);
    if(rc||done!=sizeof(cbw))goto reset;
    if(length){
        rc=api->usb_bulk(u->dev,in?u->in:u->out,data,length,&transferred);
        if(rc==-EK_EPIPE){if(api->usb_clear_halt(u->dev,in?u->in:u->out))goto reset;}
        else if(rc)goto reset;
    }
    rc=api->usb_bulk(u->dev,u->in,csw,sizeof(csw),&done);
    if(rc==-EK_EPIPE&&!api->usb_clear_halt(u->dev,u->in))rc=api->usb_bulk(u->dev,u->in,csw,sizeof(csw),&done);
    if(rc||done!=sizeof(csw)||le32(csw)!=0x53425355||le32(csw+4)!=tag||csw[12]>2||le32(csw+8)>length)goto reset;
    if(csw[12]==2)goto reset;
    if(csw[12]==1)return COMMAND_FAILED;
    if(transferred+le32(csw+8)!=length)goto reset;
    if(actual)*actual=transferred;
    return 0;
reset:
    if(rc==-EK_ENODEV){offline(u);return rc;}
    (void)recovery(u);return rc&&rc!=-EK_EPIPE?rc:-EK_EIO;
}
static int command(struct unit *u,const uint8_t *cdb,unsigned int cdb_len,bool in,void *data,unsigned int length,unsigned int *actual)
{
    for(unsigned int retry=0;retry<3;++retry){
        int rc=transport(u,cdb,cdb_len,in,data,length,actual);if(rc!=COMMAND_FAILED)return rc;
        uint8_t sense[18]={0},request[6]={3,0,0,0,18,0};unsigned int n=0;
        if(transport(u,request,6,true,sense,sizeof(sense),&n)||n<14)return -EK_EIO;
        unsigned int key=sense[2]&15;
        if(key==6){if(u->exposed){offline(u);return -EK_ENODEV;}continue;}
        if(key==2){if(sense[12]==0x3a){offline(u);return -EK_ENODEV;}api->sleep_ms(100);continue;}
        if(key==7){u->block.read_only=true;return -EK_EROFS;}
        if(key==5)return -EK_ENOTSUP;
        return -EK_EIO;
    }
    return -EK_EIO;
}
static int claim(struct unit *u)
{
    uint64_t until=api->now_ms()+5000;
    do{
        if(!u->online)return -EK_ENODEV;
        int rc=api->usb_begin(u->dev);
        if(!rc){if(!u->online){api->usb_end();return -EK_ENODEV;}return 0;}
        if(rc!=-EK_EBUSY)return rc;
        api->sleep_ms(1);
    }while(api->now_ms()<until);
    return -EK_EBUSY;
}
static int block_init_unit(struct block_device *d){return ((struct unit *)d->private_data)->online?0:-EK_ENODEV;}
static int rw(struct block_device *d,uint64_t lba,uint32_t count,void *data,bool write)
{
    struct unit *u=d->private_data;int rc=claim(u);if(rc)return rc;
    while(count){
        unsigned int blocks=4096/d->block_size;if(blocks>count)blocks=count;
        uint8_t cdb[16]={0};unsigned int cdb_len;
        if(lba<=0xFFFFFFFFULL&&blocks<=0xFFFFFFFFULL-lba+1){
            cdb_len=10;cdb[0]=write?0x2a:0x28;putbe32(cdb+2,(uint32_t)lba);cdb[7]=(uint8_t)(blocks>>8);cdb[8]=(uint8_t)blocks;
        }else{cdb_len=16;cdb[0]=write?0x8a:0x88;putbe64(cdb+2,lba);putbe32(cdb+10,blocks);}
        unsigned int expected=blocks*d->block_size,done=0;rc=command(u,cdb,cdb_len,!write,data,expected,&done);
        if(rc||done!=expected){if(!rc)rc=-EK_EIO;break;}
        lba+=blocks;count-=blocks;data=(uint8_t *)data+expected;
    }
    api->usb_end();return rc;
}
static int read_blocks(struct block_device *d,uint64_t lba,uint32_t n,void *buf){return rw(d,lba,n,buf,false);}
static int write_blocks(struct block_device *d,uint64_t lba,uint32_t n,const void *buf){return rw(d,lba,n,(void *)buf,true);}
static int sync_blocks(struct block_device *d)
{
    struct unit *u=d->private_data;int rc=claim(u);if(rc)return rc;
    uint8_t cdb[10]={0x35};rc=command(u,cdb,10,false,NULL,0,NULL);api->usb_end();return rc;
}
static const struct block_device_ops block_ops={.init=block_init_unit,.read=read_blocks,.write=write_blocks,.sync=sync_blocks};
static void probe(struct context *ctx)
{
    for(unsigned int lun=0;lun<=ctx->max_lun;++lun){
        struct unit *u=NULL;
        for(unsigned int i=0;i<UNITS;++i)if(units[i].used&&units[i].dev==ctx->dev&&units[i].iface==ctx->iface&&units[i].lun==lun&&units[i].online){u=&units[i];break;}
        if(u){uint8_t tur[6]={0};(void)command(u,tur,6,false,NULL,0,NULL);continue;}
        for(unsigned int i=0;i<UNITS;++i){
            if(units[i].used&&!units[i].online&&!units[i].block.references){
                int rc=units[i].exposed?api->block_unregister(api->owner,&units[i].block):0;
                if(!rc||rc==-EK_ENOENT)memset(&units[i],0,sizeof(units[i]));
            }
            if(!units[i].used&&!u)u=&units[i];
        }
        if(!u)return;
        *u=(struct unit){.used=true,.online=true,.dev=ctx->dev,.iface=ctx->iface,.lun=(uint8_t)lun};
        for(unsigned int i=0;i<ctx->iface->endpoint_count;++i){struct usb_endpoint *e=&ctx->iface->endpoints[i];if(e->type==2){if(e->address&128)u->in=e;else u->out=e;}}
        uint8_t inquiry[36],cdb[16]={0x12,0,0,0,36,0};unsigned int done=0;int rc=command(u,cdb,6,true,inquiry,36,&done);
        if(rc||done<36||(inquiry[0]&31)!=0)goto failed;
        memset(cdb,0,sizeof(cdb));cdb[0]=0x25;uint8_t capacity[32];
        rc=command(u,cdb,10,true,capacity,8,&done);if(rc||done!=8)goto failed;
        uint64_t last=be32(capacity);uint32_t size=be32(capacity+4);
        if(last==0xFFFFFFFFULL){memset(cdb,0,sizeof(cdb));cdb[0]=0x9e;cdb[1]=0x10;cdb[13]=32;
            rc=command(u,cdb,16,true,capacity,32,&done);if(rc||done<12)goto failed;last=be64(capacity);size=be32(capacity+8);}
        if(last==~0ULL||!size||size<512||size>4096||(size&(size-1))||last>=0x7FFFFFFFFFFFFFFFULL/size)goto failed;
        u->block=(struct block_device){.name="USB SCSI disk",.block_size=size,.block_count=last+1,.ops=&block_ops,.private_data=u,.initialized=true,.removable=true};
        memset(cdb,0,sizeof(cdb));cdb[0]=0x1a;cdb[2]=0x3f;cdb[4]=4;
        uint8_t mode[4];rc=command(u,cdb,6,true,mode,4,&done);
        if(!u->online)goto failed;
        if(!rc&&done>=3)u->block.read_only=(mode[2]&128)!=0;
        rc=api->block_register(api->owner,&u->block);if(rc)goto failed;
        u->exposed=true;api->log("USB storage ready",u->block.block_count,size);continue;
failed:
        memset(u,0,sizeof(*u));
    }
}
static int bind(struct usb_device *dev,struct usb_interface *iface)
{
    if(iface->subclass!=6||iface->protocol!=0x50||dev->speed==USB_LOW)return -EK_ENOTSUP;
    struct usb_endpoint *in=NULL,*out=NULL;
    for(unsigned int i=0;i<iface->endpoint_count;++i){struct usb_endpoint *e=&iface->endpoints[i];if(e->type!=2)continue;if(e->address&128){if(in)return -EK_EINVAL;in=e;}else{if(out)return -EK_EINVAL;out=e;}}
    if(!in||!out||in->max_packet>(dev->speed==USB_SUPER?1024:512)||out->max_packet>(dev->speed==USB_SUPER?1024:512))return -EK_ENOTSUP;
    for(unsigned int i=0;i<CONTEXTS;++i)if(!contexts[i].used){
        uint8_t max=0;unsigned int done=0;
        int rc=api->usb_control(dev,0xa1,254,0,iface->number,&max,1,&done);
        if(rc==-EK_EPIPE||rc==-EK_EIO){max=0;}else if(rc||done!=1||max>15)return -EK_EIO;
        contexts[i]=(struct context){dev,iface,max,true};iface->poll_interval_ms=1000;
        probe(&contexts[i]);return 0;
    }
    return -EK_ENOSPC;
}
static void poll(struct usb_device *dev,struct usb_interface *iface)
{for(unsigned int i=0;i<CONTEXTS;++i)if(contexts[i].used&&contexts[i].dev==dev&&contexts[i].iface==iface)probe(&contexts[i]);}
static void disconnect(struct usb_device *dev,struct usb_interface *iface)
{
    for(unsigned int i=0;i<UNITS;++i)if(units[i].used&&units[i].dev==dev&&units[i].iface==iface)offline(&units[i]);
    for(unsigned int i=0;i<CONTEXTS;++i)if(contexts[i].used&&contexts[i].dev==dev&&contexts[i].iface==iface)memset(&contexts[i],0,sizeof(contexts[i]));
}
static const struct usb_class_driver driver={8,"usb-storage",bind,poll,disconnect};
int usb_msc_init(const struct ek_driver_api *a)
{if(!a||a->abi!=EK_DRIVER_ABI||a->size<sizeof(*a))return -EK_EINVAL;api=a;return api->usb_register_class(api->owner,&driver);}
int usb_msc_fini(void)
{
    for(unsigned int i=0;i<UNITS;++i)if(units[i].used&&units[i].block.references)return -EK_EBUSY;
    int rc=api->usb_unregister_class(api->owner,&driver);if(rc&&rc!=-EK_ENOENT)return rc;
    for(unsigned int i=0;i<UNITS;++i)if(units[i].exposed){rc=api->block_unregister(api->owner,&units[i].block);if(rc)return rc;memset(&units[i],0,sizeof(units[i]));}
    return 0;
}
