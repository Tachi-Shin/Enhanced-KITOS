#include <drivers/video/video.h>
#include <kernel/error.h>
#include <lib/string.h>
static const uint32_t colors[16]={0x101820,0xcd3131,0x0dbc79,0xe5c07b,0x2472c8,0xbc3fbc,0x11a8cd,0xd0d7de,
    0x666666,0xf14c4c,0x23d18b,0xf5f543,0x3b8eea,0xd670d6,0x29b8db,0xffffff};
static uint32_t palette(unsigned int n)
{
    if(n<16)return colors[n];
    if(n>=232){unsigned int c=8+(n-232)*10;return c*0x010101;}
    n-=16;unsigned int r=n/36,g=n/6%6,b=n%6;
    return ((r?55+r*40:0)<<16)|((g?55+g*40:0)<<8)|(b?55+b*40:0);
}
static uint32_t encode(const struct video_surface *s,uint32_t rgb)
{
    unsigned int r=rgb>>16&255,g=rgb>>8&255,b=rgb&255;
    if(!s->order){unsigned int tmp=r;r=b;b=tmp;}
    return s->depth==16?((r>>3)<<11)|((g>>2)<<5)|(b>>3):r|(g<<8)|(b<<16);
}
static void raw_put(struct video_terminal *t,unsigned int x,unsigned int y,uint32_t raw)
{
    unsigned int n=t->fb.depth/8;uint8_t *p=t->fb.pixels+(size_t)y*t->fb.pitch+x*n;
    for(unsigned int i=0;i<n;++i)p[i]=(uint8_t)(raw>>(i*8));
}
static uint32_t raw_get(struct video_terminal *t,unsigned int x,unsigned int y)
{
    unsigned int n=t->fb.depth/8;const uint8_t *p=t->fb.pixels+(size_t)y*t->fb.pitch+x*n;uint32_t v=0;
    for(unsigned int i=0;i<n;++i)v|=(uint32_t)p[i]<<(i*8);
    return v;
}
static uint32_t fg(struct video_terminal *t){return encode(&t->fb,t->inverse?t->background:t->foreground);}
static uint32_t bg(struct video_terminal *t){return encode(&t->fb,t->inverse?t->foreground:t->background);}
static void cursor(struct video_terminal *t,bool show)
{
    if(t->cursor_drawn==show||(!t->cursor_visible&&show))return;
    unsigned int x=t->x*6,y=t->y*8+7;
    for(unsigned int i=0;i<6;++i){
        if(show){t->cursor_pixels[i]=raw_get(t,x+i,y);raw_put(t,x+i,y,fg(t));}
        else raw_put(t,x+i,y,t->cursor_pixels[i]);
    }
    t->cursor_drawn=show;
}
static void fill(struct video_terminal *t,unsigned int first,unsigned int last)
{
    uint32_t color=bg(t);
    for(unsigned int cell=first;cell<last;++cell){unsigned int x=cell%t->columns*6,y=cell/t->columns*8;
        for(unsigned int j=0;j<8;++j)for(unsigned int i=0;i<6;++i)raw_put(t,x+i,y+j,color);
    }
}
static void newline(struct video_terminal *t)
{
    t->x=0;t->wrap=false;
    if(++t->y<t->rows)return;
    t->y=t->rows-1;
    size_t n=(size_t)(t->rows-1)*8*t->fb.pitch,offset=8*t->fb.pitch;
    for(size_t i=0;i<n;++i)t->fb.pixels[i]=t->fb.pixels[i+offset];
    fill(t,(t->rows-1)*t->columns,t->rows*t->columns);
}
static void glyph(struct video_terminal *t,unsigned char c)
{
    if(t->wrap)newline(t);
    unsigned int x=t->x*6,y=t->y*8;uint32_t a=fg(t),b=bg(t);
    for(unsigned int j=0;j<8;++j){uint8_t row=video_font_row(c,j);
        for(unsigned int i=0;i<6;++i)raw_put(t,x+i,y+j,i<5&&(row&(16U>>i))?a:b);
    }
    if(t->x+1==t->columns)t->wrap=true;else ++t->x;
}
static unsigned int param(struct video_terminal *t,unsigned int i,unsigned int fallback)
{ return i<t->count&&t->params[i]?t->params[i]:fallback; }
static void sgr(struct video_terminal *t)
{
    for(unsigned int i=0;i<t->count;++i){unsigned int p=t->params[i];
        if(!p){t->foreground=colors[7];t->background=colors[0];t->bold=t->inverse=false;}
        else if(p==1)t->bold=true;else if(p==22)t->bold=false;else if(p==7)t->inverse=true;else if(p==27)t->inverse=false;
        else if(p==39)t->foreground=colors[7];else if(p==49)t->background=colors[0];
        else if(p>=30&&p<=37)t->foreground=colors[p-30+(t->bold?8:0)];
        else if(p>=40&&p<=47)t->background=colors[p-40];
        else if(p>=90&&p<=97)t->foreground=colors[p-90+8];
        else if(p>=100&&p<=107)t->background=colors[p-100+8];
        else if((p==38||p==48)&&i+2<t->count){
            uint32_t c;
            if(t->params[i+1]==5&&t->params[i+2]<256){c=palette(t->params[i+2]);i+=2;}
            else if(t->params[i+1]==2&&i+4<t->count&&t->params[i+2]<256&&t->params[i+3]<256&&t->params[i+4]<256){
                c=t->params[i+2]<<16|t->params[i+3]<<8|t->params[i+4];i+=4;
            }else continue;
            if(p==38)t->foreground=c;else t->background=c;
        }
    }
}
static unsigned int clamp(unsigned int n,unsigned int max){return n<max?n:max-1;}
static void csi(struct video_terminal *t,char c)
{
    unsigned int n=param(t,0,1),cell=t->y*t->columns+t->x;
    if(t->private_mode){if(t->params[0]==25&&(c=='h'||c=='l'))t->cursor_visible=c=='h';return;}
    switch(c){
    case 'm':sgr(t);return;
    case 'A':t->y=n>t->y?0:t->y-n;break;
    case 'B':t->y=clamp(t->y+n,t->rows);break;
    case 'C':t->x=clamp(t->x+n,t->columns);
        break;
    case 'D':t->x=n>t->x?0:t->x-n;break;
    case 'E':t->y=clamp(t->y+n,t->rows);t->x=0;break;
    case 'F':t->y=n>t->y?0:t->y-n;t->x=0;break;
    case 'G':t->x=clamp(n-1,t->columns);
        break;
    case 'H':case 'f':t->y=clamp(n-1,t->rows);t->x=clamp(param(t,1,1)-1,t->columns);
        break;
    case 'K':
        if(t->params[0]==0)fill(t,cell,(t->y+1)*t->columns);
        else if(t->params[0]==1)fill(t,t->y*t->columns,cell+1);
        else if(t->params[0]==2)fill(t,t->y*t->columns,(t->y+1)*t->columns);
        break;
    case 'J':
        if(t->params[0]==0)fill(t,cell,t->rows*t->columns);
        else if(t->params[0]==1)fill(t,0,cell+1);
        else if(t->params[0]==2||t->params[0]==3)fill(t,0,t->rows*t->columns);
        break;
    case 's':t->saved_x=t->x;t->saved_y=t->y;break;
    case 'u':t->x=t->saved_x;t->y=t->saved_y;break;
    }
    t->wrap=false;
}
int video_terminal_init(struct video_terminal *t,const struct video_surface *s)
{
    if(!t||!s||!s->pixels||s->width<6||s->height<8||s->width>4096||s->height>2160||
        (s->depth!=16&&s->depth!=24&&s->depth!=32)||s->order>1||s->pitch<(uint64_t)s->width*(s->depth/8)||
        s->pitch>64U*1024*1024/s->height||(uint64_t)s->pitch*s->height>s->bytes)return -K_EINVAL;
    memset(t,0,sizeof(*t));t->fb=*s;t->columns=s->width/6;t->rows=s->height/8;
    t->foreground=colors[7];t->background=colors[0];t->cursor_visible=true;
    memset(s->pixels,0,(size_t)s->pitch*s->height);fill(t,0,t->columns*t->rows);cursor(t,true);return 0;
}
void video_terminal_write(struct video_terminal *t,char value)
{
    unsigned char c=(unsigned char)value;cursor(t,false);
    if(t->state==3){if(c==7)t->state=0;else if(c==27)t->state=4;goto done;}
    if(t->state==4){t->state=c=='\\'?0:3;goto done;}
    if(c==27){t->state=1;t->utf_left=0;goto done;}
    if(t->state==1){
        t->state=0;
        if(c=='['){t->state=2;t->count=1;t->private_mode=false;memset(t->params,0,sizeof(t->params));}
        else if(c==']')t->state=3;
        else if(c=='c'){t->x=t->y=0;t->wrap=false;t->foreground=colors[7];t->background=colors[0];t->bold=t->inverse=false;t->cursor_visible=true;fill(t,0,t->rows*t->columns);}
        else if(c=='7'){t->saved_x=t->x;t->saved_y=t->y;}
        else if(c=='8'){t->x=t->saved_x;t->y=t->saved_y;}
        goto done;
    }
    if(t->state==2){
        if(c=='?'){t->private_mode=true;goto done;}
        if(c>='0'&&c<='9'){unsigned int *p=&t->params[t->count-1];*p=*p<6553?*p*10+c-'0':65535;goto done;}
        if(c==';'){if(t->count<16)++t->count;else t->state=5;goto done;}
        t->state=0;if(c>=0x40&&c<=0x7e)csi(t,(char)c);goto done;
    }
    if(t->state==5){if(c>=0x40&&c<=0x7e)t->state=0;goto done;}
    if(t->utf_left){if((c&0xc0)==0x80){if(!--t->utf_left){glyph(t,'?');if(t->utf_width==2)glyph(t,' ');}goto done;}t->utf_left=0;glyph(t,'?');}
    if(c>=0xc2&&c<=0xf4){t->utf_left=c<0xe0?1:c<0xf0?2:3;t->utf_width=c>=0xe3?2:1;goto done;}
    if(c>=128){glyph(t,'?');goto done;}
    if(c=='\n')newline(t);else if(c=='\r'){t->x=0;t->wrap=false;}
    else if(c=='\b'){if(t->x)--t->x;t->wrap=false;}
    else if(c=='\t'){unsigned int n=8-t->x%8;while(n--)glyph(t,' ');}
    else if(c>=32&&c<127)glyph(t,c);
done:cursor(t,true);
}
