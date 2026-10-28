#include <ekitos/driver.h>
#include <drivers/video/video.h>
#include <kernel/error.h>
#include <lib/string.h>
static const struct ek_driver_api *api;
static struct video_terminal terminal;
static struct kitos_terminal_info information;
static uint8_t *allocation;
static size_t frame_bytes;
static unsigned int front,back;
static bool double_buffer,dirty;
static int flip(void);
static void text_mode(void);
static int property(uint32_t tag,void *values,unsigned int bytes,unsigned int request)
{
    uint32_t buffer[40];if(bytes>148||(bytes&3))return -K_EINVAL;
    buffer[0]=tag;buffer[1]=bytes;buffer[2]=request;memcpy(buffer+3,values,bytes);
    int rc=api->firmware_request(buffer,bytes+12);if(rc)return rc;
    if(!(buffer[2]&0x80000000U)||(buffer[2]&0x7fffffffU)>bytes)return -K_ENOTSUP;
    if((buffer[2]&0x7fffffffU)<bytes)return -K_EIO;
    memcpy(values,buffer+3,bytes);return 0;
}
static bool timing_equal(const struct video_mode *a,const struct video_mode *b)
{
    return a->clock==b->clock&&a->width==b->width&&a->height==b->height&&a->hs==b->hs&&a->he==b->he&&a->ht==b->ht&&
        a->vs==b->vs&&a->ve==b->ve&&a->vt==b->vt&&((a->flags^b->flags)&0x70f)==0;
}
static unsigned int add(uint32_t *tags,unsigned int *used,uint32_t tag,unsigned int n,const uint32_t *values)
{
    unsigned int off=*used;tags[off]=tag;tags[off+1]=n*4;tags[off+2]=n*4;
    for(unsigned int i=0;i<n;++i)tags[off+3+i]=values?values[i]:0;
    *used+=3+n;return off;
}
static bool response(const uint32_t *tags,unsigned int off,unsigned int n)
{return tags[off+2]==(0x80000000U|n*4);}
static int initialize(const struct ek_driver_api *a)
{
    if(!a||a->abi!=EK_DRIVER_ABI||a->size<sizeof(*a)||!a->firmware_request||!a->framebuffer_map)return -K_ENOTSUP;
    api=a;uint32_t count=1;
    if(property(0x40013,&count,4,0)||!count||count>8)count=1;
    uint8_t edid[512];struct video_edid parsed={0};unsigned int display=0;
    bool virtual=!strcmp(api->platform,"rpi4/qemu");
    /* Find the first output with a valid base EDID, including HDMI1-only boot.
     * On older firmware/QEMU retain the firmware-selected display and mode. */
    for(unsigned int index=0;index<count;++index){
        uint32_t id=index;if(property(0x40016,&id,4,4))id=index?7:2;
        uint32_t block[34]={0};block[1]=id;
        int rc=property(0x30023,block,sizeof(block),8);
        if(rc){memset(block,0,sizeof(block));rc=property(0x30020,block,sizeof(block),4);}
        if(rc)continue;
        memcpy(edid,block+2,128);
        unsigned int blocks=1+edid[126];if(blocks>4)blocks=4;bool complete=true;
        for(unsigned int b=1;b<blocks;++b){
            memset(block,0,sizeof(block));block[0]=b;block[1]=id;
            rc=property(0x30023,block,sizeof(block),8);
            if(rc){memset(block,0,sizeof(block));block[0]=b;rc=property(0x30020,block,sizeof(block),4);}
            if(rc){complete=false;break;}memcpy(edid+b*128,block+2,128);
        }
        uint32_t limits[2]={0};(void)property(0x40018,limits,8,0);
        uint32_t max=(index<2?limits[index]:0)/1000;if(!max)max=165000;
        struct video_edid trial={0};
        if(complete&&!video_edid_parse(edid,blocks*128,max,&trial)){
            uint32_t selection=index;(void)property(0x48013,&selection,4,4);
            parsed=trial;display=id;break;
        }
    }
    if(!display)display=2;
    uint32_t geometry[2]={0};if(property(0x40003,geometry,8,0))return -K_ENODEV;
    struct video_mode mode={.display=(uint8_t)display};
    bool timing=!property(0x40017,&mode,sizeof(mode),sizeof(mode))&&video_mode_valid(&mode);
    if(parsed.valid&&parsed.has_mode&&timing){
        struct video_mode wanted=parsed.preferred;wanted.display=(uint8_t)display;
        wanted.refresh=(uint16_t)((video_refresh(&wanted)+500)/1000);
        if(!parsed.hdmi)wanted.flags|=1U<<9;
        else if(wanted.vic>1)wanted.flags|=1U<<8;
        struct video_mode set=wanted,actual={.display=(uint8_t)display};
        if(!property(0x48017,&set,sizeof(set),sizeof(set))&&
            !property(0x40017,&actual,sizeof(actual),sizeof(actual))&&video_mode_valid(&actual)){
            mode=actual;timing=timing_equal(&wanted,&actual);
            /* Even when firmware substitutes a mode, use its actual geometry
             * and timing instead of claiming that the request was accepted. */
            geometry[0]=mode.width;geometry[1]=mode.height;
        }
    }
    if(geometry[0]<144||geometry[0]>4096||geometry[1]<160||geometry[1]>2160)return -K_ENOTSUP;
    uint32_t tags[128],used=0,depth=32,order=1,alpha=2,alignment[2]={4096,0};
    uint32_t virtual_geometry[2]={geometry[0],geometry[1]*((uint64_t)geometry[0]*geometry[1]*8<=64U*1024*1024?2:1)};
    add(tags,&used,0x48003,2,geometry);add(tags,&used,0x48004,2,virtual_geometry);
    add(tags,&used,0x48005,1,&depth);add(tags,&used,0x48006,1,&order);add(tags,&used,0x48007,1,&alpha);
    add(tags,&used,0x48009,2,NULL);add(tags,&used,0x4800a,4,NULL);
    unsigned int alloc=add(tags,&used,0x40001,2,alignment),pitch=add(tags,&used,0x40008,1,NULL);
    unsigned int virt=add(tags,&used,0x40004,2,NULL);
    unsigned int size=add(tags,&used,0x40003,2,NULL),bits=add(tags,&used,0x40005,1,NULL),rgb=add(tags,&used,0x40006,1,NULL);
    int rc=api->firmware_request(tags,used*4);if(rc)return rc;
    if(!response(tags,alloc,2)||!response(tags,pitch,1)||!response(tags,size,2)||!response(tags,bits,1)||!response(tags,rgb,1))return -K_EIO;
    struct video_surface s={.bytes=tags[alloc+4],.width=tags[size+3],.height=tags[size+4],
        .pitch=tags[pitch+3],.depth=tags[bits+3],.order=tags[rgb+3]};
    /* Validate before granting access to the firmware-owned allocation. */
    if(s.width<144||s.width>4096||s.height<160||s.height>2160||s.bytes>64U*1024*1024||
        (s.depth!=16&&s.depth!=24&&s.depth!=32)||s.order>1||s.pitch<(uint64_t)s.width*(s.depth/8)||
        (uint64_t)s.pitch*s.height>s.bytes)return -K_EIO;
    s.pixels=api->framebuffer_map(api->owner,tags[alloc+3]&0x3fffffffU,s.bytes);
    if(!s.pixels)return -K_ENOMEM;
    allocation=s.pixels;frame_bytes=(size_t)s.pitch*s.height;front=0;back=1;
    double_buffer=response(tags,virt,2)&&tags[virt+3]>=s.width&&tags[virt+4]>=2*s.height&&2*frame_bytes<=s.bytes;
    if(double_buffer)s.pixels=allocation+frame_bytes;
    s.bytes=frame_bytes;
    rc=video_terminal_init(&terminal,&s);if(rc)return rc;dirty=true;
    bool actual=video_mode_valid(&mode)&&mode.width==s.width&&mode.height==s.height;
    information=(struct kitos_terminal_info){.columns=s.width/6,.rows=s.height/8,.width=s.width,.height=s.height,
        .depth=s.depth,.pitch=s.pitch,.pixel_order=s.order,.display_id=display,.advertised_bpc=parsed.bpc,
        .refresh_millihz=actual?video_refresh(&mode):0,.flags=KITOS_DISPLAY_CONNECTED|
        (parsed.valid?KITOS_DISPLAY_EDID:0)|(actual&&timing?KITOS_DISPLAY_TIMING_VERIFIED:0)|
        (virtual?KITOS_DISPLAY_VIRTUAL:0)|(actual?KITOS_DISPLAY_LINK_KNOWN:0)|(actual&&(mode.flags&(1U<<8))?KITOS_DISPLAY_RGB_LIMITED:0)|
        (actual&&(mode.flags&(1U<<9))?KITOS_DISPLAY_DVI:0)};
    if(double_buffer){information.flags|=KITOS_DISPLAY_DOUBLE_BUFFER;if(flip())text_mode();else memcpy(terminal.fb.pixels,allocation+front*frame_bytes,frame_bytes);}
    return 0;
}
/* Firmware virtual offset is read back; accepting a request alone is not
 * evidence of page flipping. VSync timing is not inferred from this ACK. */
static int flip(void)
{
    if(!double_buffer)return -K_ENOTSUP;
    uint32_t offset[2]={0,back*terminal.fb.height};
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    int rc=property(0x48009,offset,8,8);
    uint32_t actual[2]={0};int readback=property(0x40009,actual,8,0);
    if(rc||readback||actual[0]||actual[1]!=back*terminal.fb.height){
        if(!readback&&!actual[0]&&(actual[1]==0||actual[1]==terminal.fb.height))front=actual[1]/terminal.fb.height;
        double_buffer=false;information.flags&=~KITOS_DISPLAY_DOUBLE_BUFFER;return -K_EIO;
    }
    front=back;back^=1;terminal.fb.pixels=allocation+back*frame_bytes;dirty=false;return 0;
}
static void text_mode(void)
{
    struct video_surface s=terminal.fb;
    s.pixels=allocation+(double_buffer?back:front)*frame_bytes;s.bytes=frame_bytes;
    (void)video_terminal_init(&terminal,&s);dirty=true;
}
static void write_char(char c){video_terminal_write(&terminal,c);dirty=true;}
static void poll_video(void)
{
    if(!dirty)return;
    if(!double_buffer){dirty=false;return;}
    if(flip()){text_mode();dirty=false;return;}
    /* Keep the next back buffer consistent for incremental text updates. */
    memcpy(terminal.fb.pixels,allocation+front*frame_bytes,frame_bytes);
}
static int frame(const struct ek_video_frame *f)
{
    if(!double_buffer)return -K_ENOTSUP;
    if(!f||!f->read||f->width!=terminal.fb.width||f->height!=terminal.fb.height||f->pitch<(uint64_t)f->width*4)return -K_EINVAL;
    uint8_t row[4096*4];unsigned int bytes=terminal.fb.depth/8;
    for(unsigned int y=0;y<f->height;++y){
        int rc=f->read(f->context,(uint64_t)y*f->pitch,row,f->width*4);if(rc)return rc;
        uint8_t *p=allocation+back*frame_bytes+(size_t)y*terminal.fb.pitch;
        for(unsigned int x=0;x<f->width;++x){
            uint8_t b=row[x*4],g=row[x*4+1],r=row[x*4+2];
            if(!terminal.fb.order){uint8_t t=r;r=b;b=t;}
            if(bytes==2){uint16_t rgb=(uint16_t)((r>>3)<<11|(g>>2)<<5|(b>>3));p[x*2]=rgb;p[x*2+1]=rgb>>8;}
            else {p[x*bytes]=r;p[x*bytes+1]=g;p[x*bytes+2]=b;if(bytes==4)p[x*bytes+3]=255;}
        }
    }
    return flip();
}
static void get_info(struct kitos_terminal_info *out){*out=information;}
static const struct ek_video_ops ops={write_char,poll_video,get_info,frame,text_mode};
static int init(const struct ek_driver_api *a)
{int rc=initialize(a);return rc?rc:a->device_publish(a->owner,EK_DEVICE_VIDEO,&ops);}
const struct ek_driver_manifest ek_driver_manifest={EK_DRIVER_MAGIC,EK_DRIVER_ABI,sizeof(struct ek_driver_manifest),"video",init,NULL};
