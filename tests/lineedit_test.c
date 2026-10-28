#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../usr/runtime/lineedit.h"
static unsigned int checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"lineedit:%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static struct sh_history history;
struct stream {const char *in;size_t pos;char out[65536];size_t used;};
static int input(void *v,char *c){struct stream *s=v;if(!s->in[s->pos])return 0;*c=s->in[s->pos++];return 1;}
static void output(void *v,const char *p,size_t n){struct stream *s=v;CHECK(n<sizeof(s->out)-s->used);memcpy(s->out+s->used,p,n);s->used+=n;s->out[s->used]=0;}
static void prompt(void *v){output(v,"root# ",6);}
static int complete(void *v,const char *s,size_t pos,struct sh_completion *r)
{(void)v;(void)s;r->begin=0;r->end=pos;strcpy(r->replacement,"echo ");r->total=r->count=1;return 1;}
static int edit(const char *in,char *out,size_t cap)
{
    struct stream s={.in=in};struct sh_edit_io io={&s,input,output,prompt,complete,32,6};
    return sh_edit_read(&io,&history,out,cap);
}
static void line(const char *in,const char *expected)
{char out[SH_LINE_MAX];CHECK(edit(in,out,sizeof(out))==1);CHECK(!strcmp(out,expected));}
int main(void)
{
    for(unsigned int i=0;i<80;++i){char s[64];snprintf(s,sizeof(s),"echo %u",i);sh_history_add(&history,s);}
    CHECK(history.count==64);CHECK(!strcmp(sh_history_get(&history,0),"echo 16"));
    sh_history_add(&history,"echo 79");CHECK(history.count==64);CHECK(!sh_history_get(&history,64));
    line("\033[A\n","echo 79");line("\033[A\033[A\n","echo 78");line("draft\033[A\033[B\n","draft");
    line("ac\033[Db\n","abc");line("abc\001Z\005Q\n","ZabcQ");
    line("abc\033[H\033[3~\033[F\177\n","b");line("abc def\027Z\n","abc Z");
    line("abc\002\025Z\n","Zc");line("abc\002\013\n","ab");line("ec\tcompleted\n","echo completed");
    line("echo caf\303\251\177e\n","echo cafe");line("echo \346\227\245\346\234\254\033[D\177\n","echo \346\234\254");
    CHECK(sh_text_width("ASCII",5)==5);CHECK(sh_text_width("\346\227\245\346\234\254",6)==4);
    CHECK(sh_text_width("\300\200\200\200\200\200\200\200",8)>0);
    char out[SH_LINE_MAX];CHECK(edit("\003",out,sizeof(out))==-1);CHECK(edit("\004",out,sizeof(out))==0);
    CHECK(edit("abcde\n",out,4)==-2);CHECK(edit("\033[A\n",out,4)==-2);
    char random[2048];srand(1);
    for(unsigned int trial=0;trial<3000;++trial){
        unsigned int n=(unsigned int)rand()%2000;
        for(unsigned int i=0;i<n;++i)random[i]=(char)(1+rand()%255);
        random[n++]='\n';random[n]=0;
        (void)edit(random,out,sizeof(out));CHECK(memchr(out,0,sizeof(out))!=NULL);
    }
    printf("Line editor/history/UTF-8/bounds/fuzz: %u checks PASS\n",checks);return 0;
}
