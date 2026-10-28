#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <drivers/video/video.h>
#include <ekitos/driver.h>
static unsigned int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"video:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t pixels[2*1920*1080*4+64],edid[256];
static void checksum(uint8_t *p){p[127]=0;unsigned int sum=0;for(unsigned int i=0;i<127;++i)sum+=p[i];p[127]=(uint8_t)(0-sum);}
static struct video_mode hd={.clock=148500,.width=1920,.hs=2008,.he=2052,.ht=2200,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3};
static void make_edid(void)
{
    memset(edid,0,sizeof(edid));memset(edid+1,255,6);edid[18]=1;edid[19]=4;edid[20]=0xa0;edid[24]=2;
    uint8_t *p=edid+54;unsigned int c=hd.clock/10,hb=hd.ht-hd.width,vb=hd.vt-hd.height,hs=hd.hs-hd.width,he=hd.he-hd.hs,vs=hd.vs-hd.height,ve=hd.ve-hd.vs;
    p[0]=c;p[1]=c>>8;p[2]=hd.width;p[3]=hb;p[4]=(hd.width>>8)<<4|(hb>>8);
    p[5]=hd.height;p[6]=vb;p[7]=(hd.height>>8)<<4|(vb>>8);p[8]=hs;p[9]=he;p[10]=vs<<4|ve;
    p[11]=(hs>>8)<<6|(he>>8)<<4|(vs>>4)<<2|(ve>>4);p[17]=0x1e;
    edid[126]=1;uint8_t *e=edid+128;e[0]=2;e[1]=3;e[2]=10;e[4]=0x41;e[5]=0x90;e[6]=0x63;e[7]=3;e[8]=12;e[9]=0;
    checksum(edid);checksum(e);
}
static unsigned int selected,set_count,width,height;static bool no_edid,no_timing,bad_fb,hdmi1,substitute;
static unsigned int vheight,yoffset;static bool no_double,no_flip;
static struct video_mode active;
static const struct ek_video_ops *published;
static int firmware(void *v,uint32_t bytes)
{
    uint32_t *tags=v;
    for(unsigned int off=0;off<bytes/4;){
        uint32_t *t=tags+off,*p=t+3;unsigned int n=t[1]/4;bool known=true;
        switch(t[0]){
        case 0x40013:p[0]=2;break;case 0x40016:p[0]=p[0]?7:2;break;
        case 0x30023:
            if(no_edid||(hdmi1&&p[1]!=7)||p[0]>1){known=false;break;}
            memcpy(p+2,edid+p[0]*128,128);break;
        case 0x30020:known=false;break;
        case 0x40018:p[0]=p[1]=300000000;break;
        case 0x48013:selected=p[0];break;
        case 0x40017:if(no_timing)known=false;else memcpy(p,&active,36);break;
        case 0x48017:CHECK(!no_timing);++set_count;if(!substitute)memcpy(&active,p,36);break;
        case 0x48003:width=p[0];height=p[1];break;
        case 0x40003:p[0]=width;p[1]=height;break;
        case 0x48004:vheight=no_double?height:p[1];break;
        case 0x40004:p[0]=width;p[1]=vheight;break;
        case 0x48009:if(!no_flip)yoffset=p[1];p[0]=0;p[1]=yoffset;break;
        case 0x40009:p[0]=0;p[1]=yoffset;break;
        case 0x48005:case 0x48006:case 0x48007:case 0x4800a:break;
        case 0x40001:p[0]=0xc0100000;p[1]=width*vheight*4;break;
        case 0x40008:p[0]=bad_fb?1:width*4;break;
        case 0x40005:p[0]=32;break;case 0x40006:p[0]=1;break;
        default:CHECK(0);
        }
        t[2]=known?0x80000000U|n*4:0;off+=3+n;
    }
    return 0;
}
static void *mapping(void *owner,uint64_t physical,size_t bytes)
{(void)owner;CHECK(physical==0x100000);CHECK(bytes<=sizeof(pixels)-64);return pixels;}
static int publish(void *owner,enum ek_device_kind kind,const void *ops)
{(void)owner;CHECK(kind==EK_DEVICE_VIDEO);published=ops;return 0;}
static void reset(void)
{
    no_edid=no_timing=bad_fb=hdmi1=substitute=no_double=no_flip=false;yoffset=0;vheight=480;selected=set_count=0;width=640;height=480;published=NULL;
    active=(struct video_mode){.display=2,.clock=25175,.width=640,.hs=656,.he=752,.ht=800,.height=480,.vs=490,.ve=492,.vt=525};
}
static uint32_t source_color;
static int read_frame(void *c,uint64_t off,void *out,unsigned int bytes)
{(void)c;(void)off;for(unsigned int i=0;i<bytes/4;++i)((uint32_t *)out)[i]=source_color;return 0;}
static int bad_frame(void *c,uint64_t off,void *out,unsigned int bytes)
{(void)c;(void)off;(void)out;(void)bytes;return -5;}
static void emit(struct video_terminal *t,const char *s){while(*s)video_terminal_write(t,*s++);}
int main(void)
{
    make_edid();struct video_edid e;CHECK(!video_edid_parse(edid,sizeof(edid),300000,&e));
    CHECK(e.valid&&e.has_mode&&e.hdmi&&e.bpc==8);CHECK(e.preferred.width==1920&&e.preferred.vic==16);CHECK(video_refresh(&e.preferred)==60000);
    CHECK(!video_edid_parse(edid,sizeof(edid),100000,&e));CHECK(!e.has_mode);
    edid[54]^=1;CHECK(video_edid_parse(edid,sizeof(edid),300000,&e)<0);edid[54]^=1;
    CHECK(video_edid_parse(edid,128,300000,&e)<0);
    struct ek_driver_api api={.abi=EK_DRIVER_ABI,.size=sizeof(api),.platform="test",.firmware_request=firmware,.framebuffer_map=mapping,.device_publish=publish};
    struct kitos_terminal_info info;
    reset();CHECK(!ek_driver_manifest.init(&api));CHECK(set_count==1&&published);published->info(&info);
    CHECK(info.width==1920&&info.height==1080&&info.refresh_millihz==60000);CHECK((info.flags&22)==22);CHECK(info.advertised_bpc==8);
    reset();hdmi1=true;CHECK(!ek_driver_manifest.init(&api));published->info(&info);CHECK(selected==1&&info.display_id==7);
    reset();no_edid=no_timing=true;api.platform="rpi4/qemu";CHECK(!ek_driver_manifest.init(&api));published->info(&info);
    CHECK(info.width==640&&info.refresh_millihz==0&&(info.flags&KITOS_DISPLAY_VIRTUAL));CHECK(!set_count);
    reset();bad_fb=true;CHECK(ek_driver_manifest.init(&api)<0);CHECK(!published);
    reset();substitute=true;CHECK(!ek_driver_manifest.init(&api));published->info(&info);CHECK(info.width==640&&info.refresh_millihz==59940);CHECK(!(info.flags&KITOS_DISPLAY_TIMING_VERIFIED));
    reset();CHECK(!ek_driver_manifest.init(&api));published->info(&info);CHECK(info.flags&KITOS_DISPLAY_DOUBLE_BUFFER);
    struct ek_video_frame f={NULL,read_frame,info.width,info.height,info.width*4};
    source_color=0xff0000;CHECK(!published->frame(&f));unsigned int visible=yoffset;CHECK(pixels[(size_t)visible*info.pitch]==255&&pixels[(size_t)visible*info.pitch+2]==0);
    source_color=0x0000ff;CHECK(pixels[(size_t)visible*info.pitch]==255);f.read=bad_frame;CHECK(published->frame(&f)<0);CHECK(yoffset==visible);f.read=read_frame;
    CHECK(!published->frame(&f));CHECK(yoffset!=visible);CHECK(pixels[(size_t)yoffset*info.pitch]==0&&pixels[(size_t)yoffset*info.pitch+2]==255);
    no_flip=true;CHECK(published->frame(&f)<0);published->info(&info);CHECK(!(info.flags&KITOS_DISPLAY_DOUBLE_BUFFER));published->text_mode();published->write('A');published->poll();
    reset();no_double=true;CHECK(!ek_driver_manifest.init(&api));published->info(&info);CHECK(!(info.flags&KITOS_DISPLAY_DOUBLE_BUFFER));CHECK(published->frame(&f)<0);
    for(unsigned int depth=16;depth<=32;depth+=8)for(unsigned int order=0;order<2;++order){
        unsigned int pitch=73*(depth/8)+5;size_t n=pitch*35;
        memset(pixels,0xa5,sizeof(pixels));struct video_surface s={pixels+32,n,73,35,pitch,depth,order};struct video_terminal t;
        CHECK(!video_terminal_init(&t,&s));CHECK(t.columns==12&&t.rows==4);
        emit(&t,"\033[?25l\033[2J\033[H\033[38;2;255;0;0mA");
        unsigned int x=1,y=0;uint8_t *p=s.pixels+y*pitch+x*(depth/8);
        if(depth==16)CHECK((p[0]|p[1]<<8)==(order?0xf800:0x001f));
        else {CHECK(p[order?0:2]==255);CHECK(p[1]==0);CHECK(p[order?2:0]==0);}
        emit(&t,"\033]133;B\a\033[2;3H\033[sZ\033[H\033[u");CHECK(t.x==2&&t.y==1);
        emit(&t,"\033[999999999999999999999D\033[9999999999B\033[2K");CHECK(t.x==0&&t.y==3);
        for(unsigned int i=0;i<80;++i)emit(&t,"ASCII scroll\n");
        srand(3);for(unsigned int i=0;i<50000;++i)video_terminal_write(&t,(char)rand());
        CHECK(t.x<t.columns&&t.y<t.rows);
        for(unsigned int i=0;i<32;++i){CHECK(pixels[i]==0xa5);CHECK(pixels[32+n+i]==0xa5);}
        s.pitch=1;CHECK(video_terminal_init(&t,&s)<0);
    }
    uint8_t random[512];srand(7);for(unsigned int i=0;i<10000;++i){for(unsigned int j=0;j<512;++j)random[j]=(uint8_t)rand();(void)video_edid_parse(random,rand()%513,300000,&e);}
    puts("EDID/preferred timing/firmware readback/HDMI1/fallback/color/font/ANSI/scroll/bounds/fuzz PASS");
    printf("Video: %u checks PASS\n",checks);return 0;
}
