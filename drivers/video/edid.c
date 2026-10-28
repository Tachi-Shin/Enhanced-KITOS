#include <drivers/video/video.h>
#include <kernel/error.h>
#include <lib/string.h>
bool video_mode_valid(const struct video_mode *m)
{
    return m->clock>=10000&&m->clock<=600000&&m->width>=144&&m->width<=4096&&m->height>=160&&m->height<=2160&&
        m->width<m->hs&&m->hs<m->he&&m->he<=m->ht&&m->height<m->vs&&m->vs<m->ve&&m->ve<=m->vt&&
        !(m->flags&((1U<<2)|(1U<<10)))&&m->vscan<=1;
}
uint32_t video_refresh(const struct video_mode *m)
{ return video_mode_valid(m)?(uint32_t)(((uint64_t)m->clock*1000000+m->ht*m->vt/2)/((uint64_t)m->ht*m->vt)):0; }
/* Progressive CTA modes used by common monitors. Other modes can be supplied
 * as EDID detailed timings, retaining their exact pixel clock and porches. */
static const struct video_mode cea[]={
    {.vic=1,.clock=25175,.width=640,.hs=656,.he=752,.ht=800,.height=480,.vs=490,.ve=492,.vt=525},
    {.vic=2,.clock=27000,.width=720,.hs=736,.he=798,.ht=858,.height=480,.vs=489,.ve=495,.vt=525,.flags=1U<<4},
    {.vic=4,.clock=74250,.width=1280,.hs=1390,.he=1430,.ht=1650,.height=720,.vs=725,.ve=730,.vt=750,.flags=3|(2U<<4)},
    {.vic=16,.clock=148500,.width=1920,.hs=2008,.he=2052,.ht=2200,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3|(2U<<4)},
    {.vic=17,.clock=27000,.width=720,.hs=732,.he=796,.ht=864,.height=576,.vs=581,.ve=586,.vt=625,.flags=1U<<4},
    {.vic=19,.clock=74250,.width=1280,.hs=1720,.he=1760,.ht=1980,.height=720,.vs=725,.ve=730,.vt=750,.flags=3|(2U<<4)},
    {.vic=31,.clock=148500,.width=1920,.hs=2448,.he=2492,.ht=2640,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3|(2U<<4)},
    {.vic=32,.clock=74250,.width=1920,.hs=2558,.he=2602,.ht=2750,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3|(2U<<4)},
    {.vic=33,.clock=74250,.width=1920,.hs=2448,.he=2492,.ht=2640,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3|(2U<<4)},
    {.vic=34,.clock=74250,.width=1920,.hs=2008,.he=2052,.ht=2200,.height=1080,.vs=1084,.ve=1089,.vt=1125,.flags=3|(2U<<4)},
    {.vic=93,.clock=297000,.width=3840,.hs=5116,.he=5204,.ht=5500,.height=2160,.vs=2168,.ve=2178,.vt=2250,.flags=3|(2U<<4)},
    {.vic=94,.clock=297000,.width=3840,.hs=4896,.he=4984,.ht=5280,.height=2160,.vs=2168,.ve=2178,.vt=2250,.flags=3|(2U<<4)},
    {.vic=95,.clock=297000,.width=3840,.hs=4016,.he=4104,.ht=4400,.height=2160,.vs=2168,.ve=2178,.vt=2250,.flags=3|(2U<<4)},
    {.vic=96,.clock=594000,.width=3840,.hs=4896,.he=4984,.ht=5280,.height=2160,.vs=2168,.ve=2178,.vt=2250,.flags=3|(2U<<4)},
    {.vic=97,.clock=594000,.width=3840,.hs=4016,.he=4104,.ht=4400,.height=2160,.vs=2168,.ve=2178,.vt=2250,.flags=3|(2U<<4)}
};
static bool detailed(const uint8_t *p,struct video_mode *m)
{
    memset(m,0,sizeof(*m));m->clock=(p[0]|p[1]<<8)*10U;
    m->width=p[2]|(p[4]&240)<<4;m->height=p[5]|(p[7]&240)<<4;
    m->ht=m->width+(p[3]|(p[4]&15)<<8);m->vt=m->height+(p[6]|(p[7]&15)<<8);
    m->hs=m->width+(p[8]|(p[11]&192)<<2);m->he=m->hs+(p[9]|(p[11]&48)<<4);
    m->vs=m->height+((p[10]>>4)|(p[11]&12)<<2);m->ve=m->vs+((p[10]&15)|(p[11]&3)<<4);
    if((p[17]&0x18)!=0x18)return false; /* separate digital sync only */
    m->flags=((p[17]&2)?1U:0U)|((p[17]&4)?2U:0U)|((p[17]&128)?4U:0U);
    if(!video_mode_valid(m))return false;
    for(unsigned int i=0;i<sizeof(cea)/sizeof(cea[0]);++i){
        const struct video_mode *c=&cea[i];
        if(m->clock==c->clock&&m->width==c->width&&m->height==c->height&&m->ht==c->ht&&m->vt==c->vt&&
            m->hs==c->hs&&m->he==c->he&&m->vs==c->vs&&m->ve==c->ve){m->vic=c->vic;m->flags=c->flags;break;}
    }
    return true;
}
static void candidate(struct video_edid *out,const struct video_mode *m,uint32_t max,bool preferred)
{
    if(!video_mode_valid(m)||m->clock>max)return;
    if(!out->has_mode||preferred){out->preferred=*m;out->has_mode=true;}
}
int video_edid_parse(const uint8_t *p,size_t bytes,uint32_t max,struct video_edid *out)
{
    static const uint8_t header[8]={0,255,255,255,255,255,255,0};
    if(!p||!out)return -K_EINVAL;
    memset(out,0,sizeof(*out));
    if(bytes<128||memcmp(p,header,8)||p[18]!=1)return -K_EINVAL;
    unsigned int blocks=1+p[126];if(blocks>4)blocks=4;
    if(bytes<blocks*128)return -K_EINVAL;
    for(unsigned int i=0;i<blocks;++i){unsigned int sum=0;for(unsigned int j=0;j<128;++j)sum+=p[i*128+j];if(sum&255)return -K_EINVAL;}
    out->valid=true;if(!max)max=165000;
    if(p[20]&128){unsigned int code=(p[20]>>4)&7;out->bpc=code>=1&&code<=6?4+code*2:0;}
    /* The first base DTD is preferred. Later timings only provide fallback. */
    bool preferred=false;
    for(unsigned int off=54;off+18<=126;off+=18){
        struct video_mode m;
        if(detailed(p+off,&m)){
            candidate(out,&m,max,false);
            if(off==54&&out->has_mode&&(p[24]&2))preferred=true;
        }
    }
    for(unsigned int b=1;b<blocks;++b){
        const uint8_t *e=p+b*128;if(e[0]!=2)continue;
        unsigned int end=e[2];if(end==0)continue;if(end<4||end>127)return -K_EINVAL;
        for(unsigned int off=4;off<end;){
            unsigned int kind=e[off]>>5,n=e[off]&31;++off;if(n>end-off)return -K_EINVAL;
            if(kind==3&&n>=3){unsigned int oui=e[off]|e[off+1]<<8|e[off+2]<<16;if(oui==0x000c03||oui==0xc45dd8)out->hdmi=true;}
            if(kind==2)for(unsigned int j=0;j<n;++j)for(unsigned int k=0;k<sizeof(cea)/sizeof(cea[0]);++k)
                if((e[off+j]&127)==cea[k].vic){
                    bool native=!preferred&&(e[off+j]&128)&&cea[k].clock<=max;
                    candidate(out,&cea[k],max,native);if(native)preferred=true;
                }
            off+=n;
        }
        for(unsigned int off=end;off+18<=127;off+=18){struct video_mode m;if(detailed(e+off,&m))candidate(out,&m,max,false);}
    }
    return 0;
}
