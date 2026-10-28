#include "lineedit.h"
#include <string.h>
#include <stdio.h>
static struct sh_completion completion;
const char *sh_history_get(const struct sh_history *h,unsigned int index)
{ return index<h->count?h->lines[(h->first+index)%SH_HISTORY_MAX]:NULL; }
void sh_history_add(struct sh_history *h,const char *line)
{
    if(!line||!*line||strlen(line)>=SH_LINE_MAX)return;
    if(h->count&&!strcmp(sh_history_get(h,h->count-1),line))return;
    unsigned int i=(h->first+h->count)%SH_HISTORY_MAX;
    if(h->count==SH_HISTORY_MAX)h->first=(h->first+1)%SH_HISTORY_MAX;else ++h->count;
    strcpy(h->lines[i],line);
}
static size_t next(const char *s,size_t i,size_t n)
{ if(i<n)++i;while(i<n&&((unsigned char)s[i]&0xc0)==0x80)++i;return i; }
static size_t previous(const char *s,size_t i)
{ if(i)--i;while(i&&((unsigned char)s[i]&0xc0)==0x80)--i;return i; }
unsigned int sh_text_width(const char *s,size_t n)
{
    unsigned int width=0;
    for(size_t i=0;i<n;){
        unsigned char c=(unsigned char)s[i];size_t end=next(s,i,n);unsigned int cp=c;
        if(c>=0xc0&&end-i>=2&&end-i<=4){cp=c&((1U<<(7-(unsigned int)(end-i)))-1);for(size_t p=i+1;p<end;++p)cp=(cp<<6)|((unsigned char)s[p]&63);}
        width+=cp>=0x1100&&((cp<=0x115f)||(cp>=0x2e80&&cp<=0xa4cf)||(cp>=0xac00&&cp<=0xd7a3)||
            (cp>=0xf900&&cp<=0xfaff)||(cp>=0xfe10&&cp<=0xfe6f)||(cp>=0xff00&&cp<=0xff60)||cp>=0x1f000)?2:1;
        i=end;
    }
    return width;
}
static void output(struct sh_edit_io *io,const char *text)
{ io->write(io->context,text,strlen(text)); }
static void redraw(struct sh_edit_io *io,const char *line,size_t length,size_t cursor,size_t *start)
{
    unsigned int available=io->columns>io->prompt_columns+2?io->columns-io->prompt_columns-2:8;
    if(cursor<*start)*start=cursor;
    while(*start<cursor&&sh_text_width(line+*start,cursor-*start)>available)*start=next(line,*start,length);
    size_t end=*start;
    while(end<length&&sh_text_width(line+*start,next(line,end,length)-*start)<=available)end=next(line,end,length);
    output(io,"\r\033[2K");io->prompt(io->context);
    io->write(io->context,line+*start,end-*start);
    unsigned int back=sh_text_width(line+cursor,end>=cursor?end-cursor:0);
    if(back){char escape[32];snprintf(escape,sizeof(escape),"\033[%uD",back);output(io,escape);}
}
int sh_edit_read(struct sh_edit_io *io,struct sh_history *history,char *line,size_t capacity)
{
    if(capacity>SH_LINE_MAX)capacity=SH_LINE_MAX;
    if(!io||!io->read||!io->write||!io->prompt||!history||!line||capacity<2)return 0;
    size_t length=0,cursor=0,start=0;unsigned int selected=history->count;
    char draft[SH_LINE_MAX]={0};int escape=0;unsigned int parameter=0;int overflow=0;
    line[0]=0;
    for(;;){
        char c;if(io->read(io->context,&c)!=1)return 0;
        int key=(unsigned char)c;
        if(escape){
            if(escape==1){if(c=='['||c=='O'){escape=2;parameter=0;continue;}escape=0;continue;}
            if(c>='0'&&c<='9'){parameter=parameter<100?parameter*10+(unsigned int)(c-'0'):100;continue;}
            if(c==';'){escape=3;continue;}
            escape=0;
            if(c=='A')key=16;else if(c=='B')key=14;else if(c=='C')key=6;else if(c=='D')key=2;
            else if(c=='H'||(c=='~'&&(parameter==1||parameter==7)))key=1;
            else if(c=='F'||(c=='~'&&(parameter==4||parameter==8)))key=5;
            else if(c=='~'&&parameter==3)key=4;else continue;
        }else if(c==27){escape=1;continue;}
        if(key=='\r'||key=='\n'){output(io,"\n");line[length]=0;return overflow?-2:1;}
        if(key==3){output(io,"^C\n");line[0]=0;return -1;}
        if(key==4&&!length){output(io,"\n");return 0;}
        if(key==16||key==14){
            if(selected==history->count)strcpy(draft,line);
            if(key==16&&selected)--selected;
            else if(key==14&&selected<history->count)++selected;
            const char *saved=selected==history->count?draft:sh_history_get(history,selected);
            if(saved){length=strlen(saved);if(length>=capacity){length=capacity-1;overflow=1;}memcpy(line,saved,length);line[length]=0;cursor=length;start=0;redraw(io,line,length,cursor,&start);}continue;
        }
        if(key=='\t'){
            if(!io->complete)continue;
            memset(&completion,0,sizeof(completion));
            if(io->complete(io->context,line,cursor,&completion)>0&&completion.begin<=cursor&&completion.end>=cursor&&completion.end<=length){
                size_t n=strlen(completion.replacement),old=completion.end-completion.begin;
                if(n<=capacity-1-(length-old)){
                    memmove(line+completion.begin+n,line+completion.end,length-completion.end+1);
                    memcpy(line+completion.begin,completion.replacement,n);length=length-old+n;cursor=completion.begin+n;
                }else output(io,"\a");
                if(completion.total>1){
                    output(io,"\n");
                    for(unsigned int i=0;i<completion.count;++i){output(io,completion.names[i]);output(io,"\n");}
                    if(completion.total>completion.count)output(io,"...\n");
                }
                redraw(io,line,length,cursor,&start);
            }else output(io,"\a");
            continue;
        }
        if(key==1)cursor=0;
        else if(key==5)cursor=length;
        else if(key==2)cursor=previous(line,cursor);
        else if(key==6)cursor=next(line,cursor,length);
        else if(key==8||key==127){
            if(!cursor)continue;
            size_t p=previous(line,cursor);memmove(line+p,line+cursor,length-cursor+1);length-=cursor-p;cursor=p;
        }else if(key==4){if(cursor==length)continue;size_t p=next(line,cursor,length);memmove(line+cursor,line+p,length-p+1);length-=p-cursor;}
        else if(key==21){memmove(line,line+cursor,length-cursor+1);length-=cursor;cursor=0;}
        else if(key==11){length=cursor;line[length]=0;}
        else if(key==23){
            size_t p=cursor;while(p&&line[p-1]==' ')p=previous(line,p);while(p&&line[p-1]!=' ')p=previous(line,p);
            memmove(line+p,line+cursor,length-cursor+1);length-=cursor-p;cursor=p;
        }else if(key>=32){
            if(length+1>=capacity){overflow=1;output(io,"\a");continue;}
            int tail=cursor==length;
            memmove(line+cursor+1,line+cursor,length-cursor+1);line[cursor++]=c;++length;
            if(tail&&!start&&sh_text_width(line,length)+io->prompt_columns+2<io->columns){io->write(io->context,&c,1);continue;}
        }else continue;
        redraw(io,line,length,cursor,&start);
    }
}
