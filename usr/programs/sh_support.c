#include "sh_support.h"
#include "kitos.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
struct sh_history sh_history;
static void history_path(char *path,size_t size)
{
    const char *file=getenv("HISTFILE"),*home=getenv("HOME");
    if(file)snprintf(path,size,"%s",file);
    else snprintf(path,size,"%s/.sh_history",home?home:"/root");
}
void sh_history_load(void)
{
    char path[2048];history_path(path,sizeof(path));if(!*path)return;
    FILE *f=fopen(path,"r");if(!f)return;
    char text[SH_LINE_MAX];unsigned int bytes=0;
    if(!fseek(f,0,SEEK_END)){
        long size=ftell(f);
        if(size>SH_HISTORY_MAX*SH_LINE_MAX){fseek(f,size-SH_HISTORY_MAX*SH_LINE_MAX,SEEK_SET);(void)fgets(text,sizeof(text),f);}
        else rewind(f);
    }
    while(bytes<SH_HISTORY_MAX*SH_LINE_MAX&&fgets(text,sizeof(text),f)){
        bytes+=(unsigned int)strlen(text);text[strcspn(text,"\r\n")]=0;sh_history_add(&sh_history,text);
    }
    fclose(f);
}
void sh_history_save(void)
{
    char path[2048];history_path(path,sizeof(path));if(!*path)return;
    FILE *f=fopen(path,"w");if(!f)return;
    for(unsigned int i=0;i<sh_history.count;++i)if(fprintf(f,"%s\n",sh_history_get(&sh_history,i))<0)break;
    fclose(f);
}
void sh_history_command(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"-c")){memset(&sh_history,0,sizeof(sh_history));sh_history_save();return;}
    for(unsigned int i=0;i<sh_history.count;++i)printf("%4u  %s\n",i+1,sh_history_get(&sh_history,i));
}
struct matches {struct sh_completion *result;const char *prefix;char common[768];};
static void candidate(struct matches *m,const char *name)
{
    if(strncmp(name,m->prefix,strlen(m->prefix)))return;
    struct sh_completion *r=m->result;
    for(unsigned int i=0;i<r->count;++i)if(!strcmp(name,r->names[i]))return;
    if(!r->total)strcpy(m->common,name);
    else {size_t n=0;while(m->common[n]&&m->common[n]==name[n])++n;m->common[n]=0;}
    ++r->total;
    if(r->count<64){strcpy(r->names[r->count++],name);}
}
static void directory(struct matches *m,const char *path,int commands,int folders)
{
    struct kitos_dirent entry;
    for(unsigned long long i=0;kitos_readdir(path,i,&entry)>0;++i){
        if(entry.name[0]=='.'&&m->prefix[0]!='.')continue;
        if(!strcmp(entry.name,".")||!strcmp(entry.name,".."))continue;
        int dir=entry.kind==KITOS_KIND_DIRECTORY;
        if((commands&&dir)||(folders&&!dir))continue;
        char name[768];size_t n=strlen(entry.name);if(n+dir>=sizeof(name))continue;
        strcpy(name,entry.name);if(dir)strcat(name,"/");candidate(m,name);
    }
}
static int compare(const void *a,const void *b){return strcmp(a,b);}
int sh_complete(void *unused,const char *line,size_t cursor,struct sh_completion *r)
{
    (void)unused;char word[SH_LINE_MAX],first[64]={0};size_t n=0,start=0;unsigned int token=0;char quote=0;
    int active=0;
    for(size_t i=0;i<cursor;++i){char c=line[i];
        if(!quote&&(c==' '||c=='\t')){if(active){if(!token){word[n]=0;snprintf(first,sizeof(first),"%.63s",word);}++token;}active=0;n=0;start=i+1;continue;}
        if(!active){start=i;active=1;}
        if(c=='\''||c=='"'){if(!quote){quote=c;continue;}if(quote==c){quote=0;continue;}}
        if(c=='\\'&&quote!='\''&&i+1<cursor)c=line[++i];
        if(c=='$'||c=='\n'||n+1==sizeof(word))return 0;
        word[n++]=c;
    }
    word[n]=0;r->begin=start;r->end=cursor;
    /* Completing in the middle of an existing word replaces the whole token. */
    char tail_quote=quote;
    while(line[r->end]){
        char c=line[r->end];if(!tail_quote&&(c==' '||c=='\t'))break;
        ++r->end;
        if(c=='\\'&&tail_quote!='\''&&line[r->end]){++r->end;continue;}
        if(c=='\''||c=='"'){if(!tail_quote)tail_quote=c;else if(tail_quote==c)tail_quote=0;}
    }
    struct matches m={.result=r};
    char base[SH_LINE_MAX]={0},dir[SH_LINE_MAX];char *slash=strrchr(word,'/');
    if(!token&&!slash){
        m.prefix=word;
        const char *builtins[]={"cd","export","unset","exit","history","run","spawn","wait"};
        for(unsigned int i=0;i<sizeof(builtins)/sizeof(builtins[0]);++i)candidate(&m,builtins[i]);
        const char *path=getenv("PATH");if(!path)path="/bin:/sbin";
        for(;;){const char *end=strchr(path,':');size_t size=end?(size_t)(end-path):strlen(path);
            if(size<sizeof(dir)){memcpy(dir,path,size);dir[size]=0;directory(&m,*dir?dir:".",1,0);}
            if(!end)break;
            path=end+1;
        }
    }else{
        if(slash){size_t size=(size_t)(slash-word)+1;memcpy(base,word,size);base[size]=0;memcpy(dir,word,size);dir[size]=0;m.prefix=slash+1;}
        else{strcpy(dir,".");m.prefix=word;}
        directory(&m,dir,0,token==1&&!strcmp(first,"cd"));
    }
    if(!r->total)return 0;
    char completed[SH_LINE_MAX];if(strlen(base)+strlen(m.common)>=sizeof(completed))return 0;
    strcpy(completed,base);strcat(completed,m.common);
    size_t out=0;
    for(const char *p=completed;*p;++p){
        if(strchr(" \t'\"\\$`|&;<>#()",*p)){if(out+2>=sizeof(r->replacement))return 0;r->replacement[out++]='\\';}
        if(out+1>=sizeof(r->replacement))return 0;
        r->replacement[out++]=*p;
    }
    if(r->total==1&&out&&completed[strlen(completed)-1]!='/'&&out+1<sizeof(r->replacement))r->replacement[out++]=' ';
    r->replacement[out]=0;qsort(r->names,r->count,sizeof(r->names[0]),compare);return 1;
}
