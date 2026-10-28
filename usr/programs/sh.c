/* Small native EL0 shell: PATH, cwd, environment, quoting and ELF spawn/wait.
 * Native pipelines use mapped-spawn; no fork or job-control emulation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include "kitos.h"
#include "sh_support.h"
extern char **environ;
static int last_status;
static unsigned int prompt_columns,terminal_columns=80;
static int colors(void)
{ const char *term=getenv("TERM");return isatty(1)&&!getenv("NO_COLOR")&&(!term||strcmp(term,"dumb")); }
static void error(const char *name)
{ int e=errno;fprintf(stderr,"sh: %s: %s (error -%d)\n",name,strerror(e),e); }
static int append(char **out,char *end,const char *text)
{
    while(*text){if(*out==end)return -1;*(*out)++=*text++;}return 0;
}
static int parse(const char *line,char **args,char *storage,size_t capacity)
{
    int count=0;char *out=storage,*end=storage+capacity-1;
    while(*line){
        while(*line==' '||*line=='\t')++line;
        if(!*line||*line=='#')break;
        if(count==16)return -1;
        args[count++]=out;char quote=0;
        while(*line&&(quote||(*line!=' '&&*line!='\t'))){
            char c=*line++;
            if(c=='\''||c=='"'){
                if(!quote){quote=c;continue;}
                if(quote==c){quote=0;continue;}
            }
            if(c=='\\'&&quote!='\''){
                if(!*line)return -1;
                c=*line++;
            }else if(c=='$'&&quote!='\''){
                char key[128];size_t n=0;
                if(*line=='?'){char value[16];++line;snprintf(value,sizeof(value),"%d",last_status);if(append(&out,end,value))return -1;continue;}
                while((*line>='A'&&*line<='Z')||(*line>='a'&&*line<='z')||(*line>='0'&&*line<='9')||*line=='_'){
                    if(n+1==sizeof(key))return -1;
                    key[n++]=*line++;
                }
                if(n){key[n]=0;const char *value=getenv(key);if(value&&append(&out,end,value))return -1;continue;}
            }else if(!quote&&(c=='|'||c=='>'||c=='<'||c=='&'||c==';'))return -1;
            if(out==end)return -1;
            *out++=c;
        }
        if(quote||out==end)return -1;
        *out++=0;
    }
    args[count]=NULL;return count;
}
static int find_command(const char *name,char *out,size_t capacity)
{
    if(strchr(name,'/')){if(strlen(name)>=capacity){errno=ENAMETOOLONG;return -1;}strcpy(out,name);return 0;}
    const char *path=getenv("PATH");if(!path)path="/bin:/sbin:/usr/bin:/usr/sbin";
    for(const char *begin=path;;){
        const char *end=strchr(begin,':');size_t n=end?(size_t)(end-begin):strlen(begin);
        if(n+strlen(name)+2<capacity){
            memcpy(out,begin,n);out[n]=0;if(n)strcat(out,"/");strcat(out,name);
            struct stat info;
            if(!stat(out,&info)&&S_ISREG(info.st_mode))return 0;
        }
        if(!end)break;
        begin=end+1;
    }
    errno=ENOENT;return -1;
}
static int launch(int argc,char **args,int background,int verbose)
{
    (void)argc;char path[2048];
    if(find_command(args[0],path,sizeof(path))<0){error(args[0]);return 127;}
    pid_t pid=kitos_spawn_with_fds(path,args,environ,NULL,0);
    if(pid<0){error(path);return errno==ENOEXEC?126:127;}
    if(verbose||background)printf("[process] pid=%d\n",pid);
    if(background)return 0;
    int status;if(kitos_waitpid(pid,&status,0)<0){error("waitpid");return 1;}
    if(verbose)printf("[process] pid=%d status=%d\n",pid,status);
    return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
static int assignment(char *value)
{
    char *equals=strchr(value,'=');if(!equals||equals==value){errno=EINVAL;return -1;}
    *equals=0;
    for(const char *p=value;*p;++p)if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||*p=='_'||(p!=value&&*p>='0'&&*p<='9'))){*equals='=';errno=EINVAL;return -1;}
    int rc=setenv(value,equals+1,1);*equals='=';return rc;
}
static int execute(int argc,char **args,int *exit_requested)
{
    if(!argc)return last_status;
    if(!strcmp(args[0],"history")){sh_history_command(argc,args);return 0;}
    if(!strcmp(args[0],"cd")){
        const char *home=getenv("HOME"),*path=argc>1?args[1]:home?home:"/";
        if(argc>2){errno=EINVAL;error("cd");return 2;}
        char previous[2048];if(!getcwd(previous,sizeof(previous)))return 1;
        if(!strcmp(path,"-")){path=getenv("OLDPWD");if(!path){errno=ENOENT;error("OLDPWD");return 1;}}
        if(chdir(path)<0){error(path);return 1;}
        char cwd[2048];if(getcwd(cwd,sizeof(cwd)))setenv("PWD",cwd,1);setenv("OLDPWD",previous,1);return 0;
    }
    if(!strcmp(args[0],"export")){
        if(argc==1){for(char **e=environ;e&&*e;++e)printf("export %s\n",*e);return 0;}
        for(int i=1;i<argc;++i)if(assignment(args[i])<0){error(args[i]);return 1;}
        return 0;
    }
    if(!strcmp(args[0],"unset")){for(int i=1;i<argc;++i)if(unsetenv(args[i])<0){error(args[i]);return 1;}return 0;}
    if(argc==1&&strchr(args[0],'=')){if(assignment(args[0])<0){error(args[0]);return 1;}return 0;}
    if(!strcmp(args[0],"exit")){
        char *end;long status=last_status;
        if(argc>2){errno=EINVAL;error("exit");return 2;}
        if(argc==2){status=strtol(args[1],&end,10);if(!*args[1]||*end){errno=EINVAL;error("exit");return 2;}}
        *exit_requested=1;return (int)(status&255);
    }
    if(!strcmp(args[0],"wait")){
        char *end;long pid=argc==2?strtol(args[1],&end,10):0;
        if(argc!=2||pid<=0||pid>0x7fffffff||*end){errno=EINVAL;error("wait");return 2;}
        int status;if(kitos_waitpid((pid_t)pid,&status,0)<0){error("wait");return 1;}
        printf("[process] pid=%ld status=%d\n",pid,status);return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    }
    if(!strcmp(args[0],"run")||!strcmp(args[0],"spawn")){
        if(argc<2){errno=EINVAL;error(args[0]);return 2;}
        return launch(argc-1,args+1,!strcmp(args[0],"spawn"),1);
    }
    return launch(argc,args,0,0);
}
static int builtin(const char *name)
{
    const char *names[]={"cd","export","unset","exit","history","wait","run","spawn"};
    for(unsigned int i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!strcmp(name,names[i]))return 1;
    return strchr(name,'=')!=NULL;
}
static int pipeline(char *text,char **stages,unsigned int count)
{
    (void)text;pid_t pids[8];unsigned int started=0;int previous=-1,last=0;
    /* Validate every stage before any side effects. */
    for(unsigned int i=0;i<count;++i){char data[4096];char *args[17];if(parse(stages[i],args,data,sizeof(data))<=0){fputs("sh: invalid pipeline\n",stderr);return 2;}}
    for(unsigned int i=0;i<count;++i){
        int ends[2]={-1,-1};char data[4096],path[2048];char *args[17],*wrapped[17];
        int n=parse(stages[i],args,data,sizeof(data));
        if(builtin(args[0])){
            if(n>14){errno=E2BIG;error("pipeline");goto failure;}
            strcpy(path,"/bin/sh");wrapped[0]="sh";wrapped[1]="--builtin";
            for(int j=0;j<=n;++j)wrapped[j+2]=args[j];
        }else if(find_command(args[0],path,sizeof(path))<0){error(args[0]);goto failure;}
        if(i+1<count&&pipe(ends)<0){error("pipe");goto failure;}
        struct kitos_fd_map maps[2];unsigned int m=0;
        if(previous>=0)maps[m++]=(struct kitos_fd_map){previous,0};
        if(ends[1]>=0)maps[m++]=(struct kitos_fd_map){ends[1],1};
        pid_t pid=kitos_spawn_with_fds(path,builtin(args[0])?wrapped:args,environ,maps,m);
        if(previous>=0){close(previous);previous=-1;}
        if(ends[1]>=0)close(ends[1]);
        if(pid<0){if(ends[0]>=0)close(ends[0]);error(path);goto failure;}
        pids[started++]=pid;previous=ends[0];
    }
    if(previous>=0)close(previous);
    for(unsigned int i=0;i<started;++i){int status;if(kitos_waitpid(pids[i],&status,0)<0){error("waitpid");last=1;}
        else if(i+1==started)last=WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    }
    return last;
failure:
    if(previous>=0)close(previous);
    for(unsigned int i=0;i<started;++i)(void)kill(pids[i],9);
    for(unsigned int i=0;i<started;++i){int status;(void)kitos_waitpid(pids[i],&status,0);}
    return 1;
}
static int line(const char *text,int *done)
{
    char split[1024];if(strlen(text)>=sizeof(split)){fputs("sh: command too long\n",stderr);return 2;}strcpy(split,text);
    char *stages[8]={split},quote=0;unsigned int count=1;
    for(char *p=split;*p;++p){
        if(*p=='\\'&&quote!='\''&&p[1]){++p;continue;}
        if(*p=='\''||*p=='"'){if(!quote)quote=*p;else if(quote==*p)quote=0;continue;}
        if(!quote&&*p=='#'&&(p==split||p[-1]==' '||p[-1]=='\t')){*p=0;break;}
        if(!quote&&*p=='|'){if(count==8||p[1]=='|'){fputs("sh: invalid pipeline\n",stderr);return 2;}*p=0;stages[count++]=p+1;}
    }
    if(count>1)return pipeline(split,stages,count);
    char storage[4096];char *args[17];int argc=parse(text,args,storage,sizeof(storage));
    if(argc<0){fputs("sh: invalid quoting, unsupported operator, or command too long\n",stderr);return 2;}
    return execute(argc,args,done);
}
static void profile(void)
{
    FILE *f=fopen("/etc/profile","r");if(!f)return;
    char text[1024];int done=0;
    while(fgets(text,sizeof(text),f)){
        text[strcspn(text,"\r\n")]=0;
        /* Finish reading each command before allowing children to mutate
         * this filesystem. Profile contains environment builtins only. */
        if(!strncmp(text,"export ",7))last_status=line(text,&done);
    }
    fclose(f);
}
static void prompt(void)
{
    char cwd[2048];if(!getcwd(cwd,sizeof(cwd)))strcpy(cwd,"?");
    const char *shown=cwd;unsigned int maximum=terminal_columns>24?terminal_columns-24:4;
    while(*shown&&sh_text_width(shown,strlen(shown))>maximum)++shown;
    while(((unsigned char)*shown&0xc0)==0x80)++shown;
    const char *ellipsis=shown==cwd?"":"...";
    prompt_columns=14+sh_text_width(shown,strlen(shown))+(shown==cwd?0:3);
    if(colors())printf("\033[1;32mroot@ekitos\033[0m:\033[1;34m%s%s\033[0m# ",ellipsis,shown);
    else printf("root@ekitos:%s%s# ",ellipsis,shown);
}
static int edit_read(void *unused,char *c){(void)unused;return (int)read(0,c,1);}
static void edit_write(void *unused,const char *p,size_t n){(void)unused;(void)write(1,p,n);}
static void edit_prompt(void *unused){(void)unused;prompt();}
int main(int argc,char **argv)
{
    setvbuf(stdin,NULL,_IONBF,0);setvbuf(stdout,NULL,_IONBF,0);setvbuf(stderr,NULL,_IONBF,0);
    if(argc>=3&&!strcmp(argv[1],"--builtin")){int done=0;if(!strcmp(argv[2],"history"))sh_history_load();return execute(argc-2,argv+2,&done);}
    if(argc==2&&!strcmp(argv[1],"--version")){
        puts("Enhanced KITOS sh: history=64 Tab=command,path,candidates IPC=pipes");return 0;
    }
    if(argc==3&&!strcmp(argv[1],"-c")){int done=0;return line(argv[2],&done);}
    if(argc!=1){fputs("usage: sh [--version | -c COMMAND]\n",stderr);return 2;}
    profile();setenv("PWD","/",1);
    sh_history_load();
    FILE *motd=fopen("/etc/motd","r");if(motd){char buffer[128];while(fgets(buffer,sizeof(buffer),motd))fputs(buffer,stdout);fclose(motd);}
    puts("[shell] /bin/sh ready (history, Tab completion)");
    int done=0;
    while(!done){
        const char *columns=getenv("COLUMNS");long width=columns?strtol(columns,NULL,10):80;
        terminal_columns=width>=24&&width<=1024?(unsigned int)width:80;
        struct kitos_terminal_info info;
        if(!kitos_tty_info(&info)&&(info.flags&KITOS_DISPLAY_CONNECTED)&&info.columns>=24&&info.columns<terminal_columns)
            terminal_columns=info.columns;
        fputs("\033]133;A\a",stdout);prompt();fputs("\033]133;B\a",stdout);char text[SH_LINE_MAX];
        struct sh_edit_io io={NULL,edit_read,edit_write,edit_prompt,sh_complete,terminal_columns,prompt_columns};
        int rc=sh_edit_read(&io,&sh_history,text,sizeof(text));
        if(!rc)return last_status;
        if(rc==-1){last_status=130;continue;}
        if(rc==-2){fputs("sh: command too long\n",stderr);last_status=2;continue;}
        sh_history_add(&sh_history,text);sh_history_save();last_status=line(text,&done);
    }
    return last_status;
}
