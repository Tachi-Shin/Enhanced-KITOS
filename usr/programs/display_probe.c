#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include "kitos.h"
static volatile uintptr_t fault_address=0x123;
static void fail(int n){fprintf(stderr,"[display] FAIL line=%d errno=%d\n",n,errno);exit(1);}
#define CHECK(x) do{if(!(x))fail(__LINE__);}while(0)
static void go(void){char c;CHECK(read(0,&c,1)==1);}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 if(argc==2&&!strcmp(argv[1],"--denied")){CHECK(kitos_display_acquire()<0&&errno==EBUSY);CHECK(kitos_display_release()<0&&errno==EACCES);return 0;}
 struct kitos_terminal_info i;CHECK(!kitos_tty_info(&i));CHECK(i.flags&KITOS_DISPLAY_DOUBLE_BUFFER);
 CHECK(!kitos_display_acquire());CHECK(kitos_display_acquire()<0&&errno==EBUSY);
 char *args[]={"/sbin/display-probe","--denied",NULL};pid_t pid=kitos_spawn(args[0],args,NULL);CHECK(pid>0);int status;CHECK(kitos_waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
 int fd=kitos_shm_open(NULL,(size_t)i.width*i.height*4,KITOS_IPC_CREATE);CHECK(fd>=0);
 volatile uint32_t *pixels=kitos_shm_map(fd,0);CHECK(pixels!=(void *)-1);
 unsigned int count=i.width*i.height;for(unsigned int n=0;n<count;++n)pixels[n]=0xff0000;
 CHECK(kitos_display_present(fd,i.width+1,i.height,i.width*4)<0&&errno==EINVAL);
 CHECK(!kitos_display_present(fd,i.width,i.height,i.width*4));puts("[display] RED_READY");go();
 if(argc==2&&!strcmp(argv[1],"--crash")){*(volatile unsigned int *)fault_address=1;return 99;}
 for(unsigned int n=0;n<count;++n)pixels[n]=0x0000ff;
 puts("[display] BLUE_STAGED");go();
 CHECK(!kitos_display_present(fd,i.width,i.height,i.width*4));puts("[display] BLUE_READY");go();
 CHECK(!kitos_shm_unmap((void *)pixels));CHECK(!close(fd));CHECK(!kitos_display_release());
 CHECK(kitos_display_release()<0&&errno==EACCES);
 puts("[display] ownership/staging/present/release PASS");return 0;
}
