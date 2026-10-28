#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "kitos.h"
extern char **environ;
static const char *command;
static int failure(const char *target)
{
    int error=errno;
    fprintf(stderr,"%s: %s: %s (error -%d)\n",command,target,strerror(error),error);
    return 1;
}
static int usage(const char *args)
{ fprintf(stderr,"usage: %s %s\n",command,args);return 2; }
static int number(const char *text,unsigned long long *out)
{
    if(!text||!*text||*text=='-')return -1;
    char *end;errno=0;unsigned long long value=strtoull(text,&end,10);
    if(errno||*end)return -1;
    *out=value;return 0;
}
static int color(void)
{ const char *term=getenv("TERM");return isatty(1)&&!getenv("NO_COLOR")&&(!term||strcmp(term,"dumb")); }
static int ls(int argc,char **argv)
{
    int detail=0,all=0,index=1;
    for(;index<argc&&argv[index][0]=='-';++index){
        for(const char *p=argv[index]+1;*p;++p){if(*p=='l')detail=1;else if(*p=='a')all=1;else return usage("[-la] [PATH ...]");}
    }
    int result=0;int count=argc-index;if(!count)count=1;
    for(int i=0;i<count;++i){
        const char *path=index<argc?argv[index+i]:".";
        struct stat info;if(stat(path,&info)<0){result=failure(path);continue;}
        if(!S_ISDIR(info.st_mode)){printf("%s\n",path);continue;}
        if(count>1)printf("%s:\n",path);
        struct kitos_dirent entry;
        for(unsigned long long n=0;;++n){
            int rc=kitos_readdir(path,n,&entry);if(rc<0){result=failure(path);break;}if(!rc)break;
            if(!all&&entry.name[0]=='.')continue;
            int dir=entry.kind==KITOS_KIND_DIRECTORY;
            if(detail)printf("%crw%crw%crw%c 1 root root %10llu ",dir?'d':'-',dir?'x':'-',dir?'x':'-',dir?'x':'-',entry.size);
            if(dir&&color())printf("\033[1;34m%s/\033[0m\n",entry.name);
            else printf("%s%s\n",entry.name,dir?"/":"");
        }
    }
    return result;
}
static int cat(int argc,char **argv)
{
    char buffer[1024];int result=0;
    for(int i=1;i<(argc<2?2:argc);++i){
        int input=argc<2||!strcmp(argv[i],"-");const char *name=input?"stdin":argv[i];
        int fd=input?0:open(name,O_RDONLY);if(fd<0){result=failure(name);continue;}
        ssize_t n;
        while((n=read(fd,buffer,sizeof(buffer)))>0){
            ssize_t done=0;while(done<n){ssize_t written=write(1,buffer+done,(size_t)(n-done));if(written<=0){result=failure("stdout");break;}done+=written;}
            if(done<n)break;
        }
        if(n<0)result=failure(name);
        if(fd!=0&&close(fd)<0)result=failure(name);
    }
    return result;
}
static int edit(int argc,char **argv)
{
    int truncate=!strcmp(command,"truncate"),touch=!strcmp(command,"touch"),append=!strcmp(command,"append");
    if(argc!=(touch?2:3))return usage(touch?"PATH":truncate?"PATH BYTES":"PATH TEXT");
    unsigned long long size=0;if(truncate&&(number(argv[2],&size)||size>0x7fffffffffffffffULL))return usage("PATH BYTES");
    int flags=O_WRONLY|(truncate?0:O_CREAT)|(append?O_APPEND:touch||truncate?0:O_TRUNC);
    int fd=open(argv[1],flags,0666);if(fd<0)return failure(argv[1]);
    int rc=0;
    if(truncate)rc=ftruncate(fd,(off_t)size);
    else if(!touch){
        size_t n=strlen(argv[2]),done=0;
        while(done<n){ssize_t count=write(fd,argv[2]+done,n-done);if(count<=0){if(!count)errno=EIO;rc=-1;break;}done+=(size_t)count;}
    }
    if(rc<0)rc=failure(argv[1]);
    if(close(fd)<0)rc=failure(argv[1]);
    return rc;
}
static int parents(const char *path)
{
    char buffer[2048];if(strlen(path)>=sizeof(buffer)){errno=ENAMETOOLONG;return -1;}
    strcpy(buffer,path);
    for(char *p=buffer+1;;++p){
        if(*p&&*p!='/')continue;
        char saved=*p;*p=0;
        if(*buffer&&mkdir(buffer,0777)<0){
            struct stat info;if(errno!=EEXIST||stat(buffer,&info)<0||!S_ISDIR(info.st_mode)){*p=saved;return -1;}
        }
        *p=saved;if(!saved)break;
    }
    return 0;
}
static int copy(const char *source,const char *target)
{
    struct stat a,b;if(stat(source,&a)<0)return failure(source);
    if(stat(target,&b)==0&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino){errno=EINVAL;return failure(target);}
    int in=open(source,O_RDONLY);if(in<0)return failure(source);
    int out=open(target,O_WRONLY|O_CREAT|O_TRUNC,0666);
    if(out<0){int error=errno;close(in);errno=error;return failure(target);}
    char bytes[1024];ssize_t n;int result=0;
    while((n=read(in,bytes,sizeof(bytes)))>0){if(write(out,bytes,(size_t)n)!=n){result=failure(target);break;}}
    if(n<0)result=failure(source);
    if(close(out)<0)result=failure(target);
    if(close(in)<0)result=failure(source);
    return result;
}
int main(int argc,char **argv)
{
    command=strrchr(argv[0],'/');command=command?command+1:argv[0];
    if(!strcmp(command,"echo")){
        int i=1,newline=1;if(argc>1&&!strcmp(argv[1],"-n")){i=2;newline=0;}
        for(int first=i;i<argc;++i)printf("%s%s",i==first?"":" ",argv[i]);
        if(newline)putchar('\n');
        return 0;
    }
    if(!strcmp(command,"pwd")){char cwd[2048];if(!getcwd(cwd,sizeof(cwd)))return failure(".");puts(cwd);return 0;}
    if(!strcmp(command,"ls"))return ls(argc,argv);
    if(!strcmp(command,"cat"))return cat(argc,argv);
    if(!strcmp(command,"touch")||!strcmp(command,"write")||!strcmp(command,"append")||!strcmp(command,"truncate"))return edit(argc,argv);
    if(!strcmp(command,"mkdir")||!strcmp(command,"rmdir")||!strcmp(command,"rm")){
        int index=1,result=0,p=0,force=0;
        if(argc>1&&!strcmp(command,"mkdir")&&!strcmp(argv[1],"-p")){p=1;++index;}
        if(argc>1&&!strcmp(command,"rm")&&!strcmp(argv[1],"-f")){force=1;++index;}
        if(index==argc)return force?0:usage("PATH ...");
        for(;index<argc;++index){int rc=!strcmp(command,"mkdir")?(p?parents(argv[index]):mkdir(argv[index],0777)):!strcmp(command,"rmdir")?rmdir(argv[index]):unlink(argv[index]);if(rc<0&&!(force&&errno==ENOENT))result=failure(argv[index]);}
        return result;
    }
    if(!strcmp(command,"mv")||!strcmp(command,"cp")){
        if(argc!=3)return usage("SOURCE TARGET");
        return !strcmp(command,"cp")?copy(argv[1],argv[2]):rename(argv[1],argv[2])<0?failure(argv[1]):0;
    }
    if(!strcmp(command,"true"))return 0;
    if(!strcmp(command,"false"))return 1;
    if(!strcmp(command,"clear")){fputs("\033[2J\033[H",stdout);return 0;}
    if(!strcmp(command,"uname")){puts(argc>1?"Enhanced KITOS ekitos aarch64":"Enhanced KITOS");return 0;}
    if(!strcmp(command,"env")){for(char **p=environ;p&&*p;++p)puts(*p);return 0;}
    if(!strcmp(command,"sleep")){
        unsigned long long seconds;if(argc!=2||number(argv[1],&seconds)||seconds>4294967)return usage("SECONDS");
        return kitos_sleep_ms((uint32_t)(seconds*1000))<0?failure("sleep"):0;
    }
    if(!strcmp(command,"help")){
        puts("Enhanced KITOS shell\n"
             "cd [DIR] | export NAME=VALUE | unset NAME | exit [STATUS] | history [-c]\n"
             "Up/Down: history | Tab: command/path completion and candidates\n"
             "Pipelines: echo hello | cat (up to 8 stages)\n"
             "ls [-la] [PATH] | pwd | cat [FILE|-] | echo TEXT | cp SOURCE TARGET\n"
             "touch PATH | mkdir [-p] PATH | rm [-f] PATH | rmdir PATH | mv OLD NEW\n"
             "write PATH TEXT | append PATH TEXT | truncate PATH BYTES\n"
             "env | uname | clear | sleep SECONDS | true | false\n"
             "lsmod | insmod PATH | rmmod NAME | disks | mount DISK PATH | umount PATH\n"
             "display | time | ps | procs | mem | uart | usb | input | gpio PIN [in|out|0|1|up|down|none]\n"
             "selftest | memtest | usertest | oomtest | stop ID | resume ID | kill ID\n"
             "run PATH [ARGS...] | spawn PATH [ARGS...] | wait PID\n"
             "Commands are ELF programs in /bin and /sbin; PATH selects them.");return 0;
    }
    static const struct {const char *name;unsigned int op;} controls[]={
        {"display",KITOS_CTL_DISPLAY},{"time",KITOS_CTL_TIME},{"ps",KITOS_CTL_PS},{"mem",KITOS_CTL_MEM},{"uart",KITOS_CTL_UART},
        {"usb",KITOS_CTL_USB},{"input",KITOS_CTL_INPUT},{"disks",KITOS_CTL_DISKS},{"lsmod",KITOS_CTL_LSMOD},
        {"insmod",KITOS_CTL_INSMOD},{"rmmod",KITOS_CTL_RMMOD},{"mount",KITOS_CTL_MOUNT},{"umount",KITOS_CTL_UMOUNT},
        {"selftest",KITOS_CTL_SELFTEST},{"memtest",KITOS_CTL_MEMTEST},{"usertest",KITOS_CTL_USERTEST},{"oomtest",KITOS_CTL_OOMTEST},
        {"procs",KITOS_CTL_PROCS},{"gpio",KITOS_CTL_GPIO},{"stop",KITOS_CTL_STOP},{"resume",KITOS_CTL_RESUME},
        {"kill",KITOS_CTL_KILL},{"tsleep",KITOS_CTL_TASK_SLEEP}};
    for(unsigned int i=0;i<sizeof(controls)/sizeof(controls[0]);++i)if(!strcmp(command,controls[i].name)){
        unsigned int op=controls[i].op;unsigned long x=0,y=0;unsigned long long value;
        if(op==KITOS_CTL_INSMOD||op==KITOS_CTL_RMMOD||op==KITOS_CTL_UMOUNT){if(argc!=2)return usage("TARGET");x=(unsigned long)argv[1];}
        else if(op==KITOS_CTL_MOUNT){if(argc!=3||number(argv[1],&value)||value>0xffffffffU)return usage("DISK PATH");x=(unsigned long)value;y=(unsigned long)argv[2];}
        else if(op>=KITOS_CTL_GPIO&&op<=KITOS_CTL_TASK_SLEEP){
            if(argc<2||number(argv[1],&value)||value>0xffffffffU)return usage("ID [ARG]");
            x=(unsigned long)value;
            if(op==KITOS_CTL_GPIO){
                const char *actions[]={"in","out","0","1","up","down","none"};
                if(argc>3)return usage("PIN [in|out|0|1|up|down|none]");
                if(argc==3){for(y=1;y<=7;++y)if(!strcmp(argv[2],actions[y-1]))break;if(y==8)return usage("PIN [in|out|0|1|up|down|none]");}
            }else if(op==KITOS_CTL_TASK_SLEEP){if(argc!=3||number(argv[2],&value)||value>0xffffffffU)return usage("ID MILLISECONDS");y=(unsigned long)value;}
            else if(argc!=2)return usage("ID");
        }else if(argc!=1)return usage("");
        long rc=kitos_control(op,x,y);if(rc<0)return failure(argc>1?argv[1]:command);
        if(op==KITOS_CTL_GPIO&&argc==2)printf("%ld\n",rc);
        return 0;
    }
    errno=ENOENT;return failure(command);
}
