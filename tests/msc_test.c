/* Fault-injected BOT device: exercise production CBW/SCSI/CSW and recovery. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/usb/msc.h>
static unsigned int checks,phase,tag,expected,command_code,lun,reset_count,clear_count;
static unsigned int inject_tag,inject_key,failed_key,last_key,last_opcode;
static unsigned int read16,write16,readonly_mode;
static unsigned int class_removed,unregister_busy;
static uint64_t now;
static struct block_device *disk;
static const struct usb_class_driver *driver;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint32_t le32(const uint8_t *p){return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void putle(uint8_t *p,uint32_t n){for(unsigned int i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static void putbe(uint8_t *p,uint32_t n){for(unsigned int i=0;i<4;++i)p[i]=(uint8_t)(n>>(24-i*8));}
static uint64_t clock_ms(void){return now;}
static void sleep_ms(uint32_t n){now+=n;}
static void log_msg(const char *s,uint64_t a,uint64_t b){(void)s;(void)a;(void)b;}
static int register_class(void *o,const struct usb_class_driver *d){(void)o;driver=d;return 0;}
static int unregister_class(void *o,const struct usb_class_driver *d){(void)o;CHECK(d==driver);if(class_removed)return -EK_ENOENT;class_removed=1;return 0;}
static int register_block(void *o,struct block_device *d){(void)o;CHECK(!disk);disk=d;return 0;}
static int unregister_block(void *o,struct block_device *d){(void)o;CHECK(d==disk&&!d->references);if(unregister_busy){unregister_busy=0;return -EK_EBUSY;}disk=NULL;return 0;}
static int begin(struct usb_device *d){(void)d;return 0;}
static void end(void){}
static int clear(struct usb_device *d,struct usb_endpoint *e){(void)d;(void)e;++clear_count;return 0;}
static int control(struct usb_device *d,uint8_t type,uint8_t req,uint16_t value,uint16_t index,void *buf,uint16_t n,unsigned int *done)
{(void)d;(void)value;(void)index;if(type==0xa1&&req==254){CHECK(n==1);*(uint8_t *)buf=0;*done=1;return 0;}CHECK(type==0x21&&req==255);++reset_count;phase=0;*done=0;return 0;}
static int bulk(struct usb_device *d,struct usb_endpoint *e,void *buf,unsigned int n,unsigned int *done)
{
    (void)d;uint8_t *p=buf;*done=n;
    if(phase==0){
        CHECK(!(e->address&128)&&n==31&&le32(p)==0x43425355&&p[14]>0&&p[14]<=16);
        tag=le32(p+4);expected=le32(p+8);command_code=p[15];lun=p[13];CHECK(lun==0);
        failed_key=command_code==3?0:inject_key;if(command_code!=3){inject_key=0;last_key=failed_key;}
        if(command_code==0x88){++read16;CHECK(p[17]==0&&p[18]==0&&p[19]==0&&p[20]==1&&p[21]==0&&p[22]==0&&p[23]==0&&p[24]==2);}
        if(command_code==0x8a)++write16;
        phase=expected?1:2;last_opcode=command_code;return 0;
    }
    if(phase==1){
        CHECK(n==expected);memset(p,0,n);
        if(command_code==0x12){CHECK(n==36);p[0]=0;}
        else if(command_code==0x25){CHECK(n==8);putbe(p,0xffffffff);putbe(p+4,4096);}
        else if(command_code==0x9e){CHECK(n==32);putbe(p,1);putbe(p+4,0x10);putbe(p+8,4096);}
        else if(command_code==0x1a){CHECK(n==4);p[2]=readonly_mode?128:0;}
        else if(command_code==3){CHECK(n==18);p[0]=0x70;p[2]=(uint8_t)last_key;p[12]=last_key==2?0x3a:0x28;}
        else if(command_code==0x88)memset(p,0xa5,n);
        phase=2;return 0;
    }
    CHECK(phase==2&&(e->address&128)&&n==13);memset(p,0,n);putle(p,0x53425355);putle(p+4,inject_tag?tag+1:tag);inject_tag=0;
    p[12]=failed_key?1:0;phase=0;return 0;
}
int main(void)
{
    struct ek_driver_api a={.abi=EK_DRIVER_ABI,.size=sizeof(a),.now_ms=clock_ms,.sleep_ms=sleep_ms,.log=log_msg,
        .usb_register_class=register_class,.usb_unregister_class=unregister_class,.block_register=register_block,.block_unregister=unregister_block,
        .usb_bulk=bulk,.usb_control=control,.usb_clear_halt=clear,.usb_begin=begin,.usb_end=end};
    struct usb_device dev={.used=true,.speed=USB_HIGH};struct usb_interface iface={.subclass=6,.protocol=0x50,.endpoint_count=2,
        .endpoints={{.address=0x81,.type=2,.max_packet=512},{.address=2,.type=2,.max_packet=512}}};
    CHECK(!usb_msc_init(&a));CHECK(!driver->bind(&dev,&iface));CHECK(disk&&disk->block_count==0x100000011ULL&&disk->block_size==4096);
    uint8_t data[4096];CHECK(!disk->ops->read(disk,0x100000002ULL,1,data));CHECK(read16==1&&data[0]==0xa5&&data[4095]==0xa5);
    CHECK(!disk->ops->write(disk,0x100000002ULL,1,data)&&write16==1);
    CHECK(!disk->ops->sync(disk)&&last_opcode==0x35);
    inject_tag=1;CHECK(disk->ops->read(disk,0x100000002ULL,1,data)==-EK_EIO);CHECK(reset_count==1&&clear_count==2);
    CHECK(!disk->ops->read(disk,0x100000002ULL,1,data));
    inject_key=7;CHECK(disk->ops->write(disk,0x100000002ULL,1,data)==-EK_EROFS&&disk->read_only);
    disk->references=1;CHECK(usb_msc_fini()==-EK_EBUSY);disk->references=0;
    inject_key=6;CHECK(disk->ops->read(disk,0x100000002ULL,1,data)==-EK_ENODEV&&disk->offline);
    CHECK(disk->ops->read(disk,0x100000002ULL,1,data)==-EK_ENODEV);
    unregister_busy=1;CHECK(usb_msc_fini()==-EK_EBUSY);CHECK(!usb_msc_fini()&&!disk);driver->disconnect(&dev,&iface);
    printf("USB BOT/SCSI capacity16/read16/write16/CSW recovery/medium change/write protect/module pin: %u checks PASS\n",checks);
}
