/* Exercise the new process filesystem ABI independently of the interactive sh. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <uapi/syscall.h>
#include "arch_syscall.h"
#include "kitos.h"
static void failed(int line)
{ fprintf(stderr,"[unix] FAIL line=%d errno=%d\n",line,errno);exit(1); }
#define CHECK(x) do {if(!(x))failed(__LINE__);}while(0)
int main(int argc,char **argv)
{
    char cwd[2048];CHECK(getcwd(cwd,sizeof(cwd)));
    if(argc==2&&!strcmp(argv[1],"child")){
        CHECK(!strcmp(cwd,"/tmp/abi"));CHECK(chdir("/root")==0);return 0;
    }
    CHECK(kitos_raw_syscall(KITOS_SYS_READDIR,(long)"/",0,-1)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_MKDIR,-1,0,0)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_RENAME,(long)"/tmp",-1,0)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_CHDIR,-1,0,0)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_GETCWD,-1,sizeof(cwd),0)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_FTRUNCATE,99,0,0)==-EBADF);
    CHECK(kitos_raw_syscall(KITOS_SYS_TTY_INFO,-1,0,0)==-EFAULT);
    CHECK(kitos_raw_syscall(KITOS_SYS_TTY_INFO,(long)&main,0,0)==-EFAULT);
    struct kitos_terminal_info terminal;CHECK(!kitos_tty_info(&terminal));
    CHECK(terminal.columns>=24&&terminal.rows>=20);
    if(terminal.flags&KITOS_DISPLAY_CONNECTED)CHECK(terminal.columns==terminal.width/6&&terminal.rows==terminal.height/8);
    errno=0;CHECK(kitos_control(0xffffffffU,0,0)==-1&&errno==EINVAL);
    CHECK(mkdir("/tmp/abi",0777)==0);CHECK(chdir("/tmp/abi")==0);
    CHECK(getcwd(cwd,sizeof(cwd))&&!strcmp(cwd,"/tmp/abi"));
    errno=0;CHECK(!getcwd(cwd,1)&&errno==ENAMETOOLONG);
    int fd=open("relative",O_WRONLY|O_CREAT|O_EXCL,0666);CHECK(fd>=0);
    CHECK(write(fd,"abc",3)==3);CHECK(ftruncate(fd,1)==0);CHECK(close(fd)==0);
    struct stat info;CHECK(stat("relative",&info)==0&&info.st_size==1&&info.st_dev!=0);
    errno=0;CHECK(chdir("relative")==-1&&errno==ENOTDIR);
    errno=0;CHECK(chdir("missing/..")==-1&&errno==ENOENT);
    CHECK(rename("relative","renamed")==0);
    struct kitos_dirent entry;int found=0,rc;
    for(unsigned long long index=0;(rc=kitos_readdir(".",index,&entry))>0;++index){
        CHECK(!entry.reserved&&memchr(entry.name,0,sizeof(entry.name)));
        if(!strcmp(entry.name,"renamed")){CHECK(entry.kind==KITOS_KIND_REGULAR&&entry.size==1);found=1;}
    }
    CHECK(rc==0&&found);
    char *args[]={"/sbin/unix-probe","child",NULL};
    pid_t pid=kitos_spawn(args[0],args,NULL);CHECK(pid>0);int status;
    CHECK(kitos_waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    CHECK(getcwd(cwd,sizeof(cwd))&&!strcmp(cwd,"/tmp/abi"));
    CHECK(unlink("renamed")==0);CHECK(chdir("../..")==0);
    CHECK(getcwd(cwd,sizeof(cwd))&&!strcmp(cwd,"/"));CHECK(rmdir("/tmp/abi")==0);
    puts("[unix] cwd/inheritance/isolation/readdir/mutation/uaccess PASS");return 0;
}
