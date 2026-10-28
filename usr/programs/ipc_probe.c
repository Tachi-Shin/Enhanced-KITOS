/* Real EL0 IPC ownership, scheduling and VM regression. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include "kitos.h"
#include "arch_syscall.h"
#include <uapi/syscall.h>
static void fail(int line){fprintf(stderr,"[ipc] FAIL line=%d errno=%d\n",line,errno);exit(1);}
#define CHECK(x) do{if(!(x))fail(__LINE__);}while(0)
static pid_t child(const char *mode,const struct kitos_fd_map *maps,unsigned n){char *args[]={"/sbin/ipc-probe",(char *)mode,NULL};pid_t p=kitos_spawn_with_fds(args[0],args,NULL,maps,n);CHECK(p>0);return p;}
static void waited(pid_t p,int code){int s;CHECK(kitos_waitpid(p,&s,0)==p);CHECK(WIFEXITED(s)&&WEXITSTATUS(s)==code);}
static void closed(int fd){CHECK(!close(fd));}
int main(int argc,char **argv){
 setvbuf(stdout,NULL,_IONBF,0);
 char data[4096],out[4096];memset(data,0x3a,sizeof(data));uint64_t v;int p[2],q[2],fd,other;pid_t pid;
 if(argc==2){
  if(!strcmp(argv[1],"copy")){ssize_t n;while((n=read(0,out,sizeof(out)))>0)CHECK(write(1,out,(size_t)n)==n);CHECK(!n);return 0;}
  if(!strcmp(argv[1],"atomic-a")||!strcmp(argv[1],"atomic-b")){memset(data,argv[1][7]=='a'?'A':'B',sizeof(data));for(unsigned int i=0;i<8;++i)CHECK(write(1,data,sizeof(data))==sizeof(data));return 0;}
  if(!strcmp(argv[1],"writer")){for(unsigned i=0;i<64;++i)CHECK(write(1,data,sizeof(data))==sizeof(data));return 0;}
  if(!strcmp(argv[1],"block-read")){CHECK(read(0,out,1)==0);return 0;}
  if(!strcmp(argv[1],"block-poll")){struct kitos_pollfd f={0,KITOS_POLLIN,0};CHECK(kitos_poll(&f,1,-1)>0);return 0;}
  if(!strcmp(argv[1],"block-accept")){CHECK(kitos_accept(3,0)>=0);return 0;}
  if(!strcmp(argv[1],"server")){int c=kitos_accept(3,0);CHECK(c>=0);CHECK(read(c,out,sizeof(out))==5&&!memcmp(out,"hello",5));CHECK(write(c,"reply",5)==5);closed(c);return 0;}
  if(!strcmp(argv[1],"shared")){CHECK(fcntl(3,F_GETFD)==0);volatile uint32_t *m=kitos_shm_map(3,0);CHECK(m!=(void *)-1&&m[0]==123);m[1024]=456;closed(3);CHECK(m[0]==123);CHECK(!kitos_shm_unmap((void *)m));return 0;}
  if(!strcmp(argv[1],"readonly")){volatile uint32_t *m=kitos_shm_map(3,KITOS_IPC_READONLY);CHECK(m!=(void *)-1);CHECK(m[0]==123);m[0]=999;return 99;}
  if(!strcmp(argv[1],"cloexec")){errno=0;CHECK(fcntl(0,F_GETFD)==-1&&errno==EBADF);return 0;}
  return 2;
 }
 CHECK(kitos_raw_syscall(KITOS_SYS_PIPE,-1,0,0)==-EFAULT);
 CHECK(!pipe(p)&&!pipe(q));struct kitos_fd_map maps[]={{p[0],0},{q[1],1}};pid=child("copy",maps,2);closed(p[0]);closed(q[1]);
 CHECK(write(p[1],"hello",5)==5);CHECK(read(q[0],out,9)==5&&!memcmp(out,"hello",5));closed(p[1]);CHECK(read(q[0],out,1)==0);closed(q[0]);waited(pid,0);
 CHECK(!pipe(p));maps[0]=(struct kitos_fd_map){p[1],1};pid=child("writer",maps,1);closed(p[1]);unsigned total=0;ssize_t n;while((n=read(p[0],out,1031))>0){for(int i=0;i<n;++i)CHECK(out[i]==0x3a);total+=(unsigned)n;}CHECK(n==0&&total==64*4096);closed(p[0]);waited(pid,0);
 CHECK(!pipe(p));maps[0]=(struct kitos_fd_map){p[1],1};pid_t wa=child("atomic-a",maps,1),wb=child("atomic-b",maps,1);closed(p[1]);unsigned records=0;while((n=read(p[0],out,sizeof(out)))>0){CHECK(n==4096&&(out[0]=='A'||out[0]=='B'));for(int i=1;i<n;++i)CHECK(out[i]==out[0]);++records;}CHECK(n==0&&records==16);closed(p[0]);waited(wa,0);waited(wb,0);
 CHECK(!kitos_pipe(p,KITOS_IPC_NONBLOCK));errno=0;CHECK(read(p[0],out,1)==-1&&errno==EAGAIN);CHECK(write(p[1],data,4096)==4096);errno=0;CHECK(write(p[1],data,1)==-1&&errno==EAGAIN);other=dup(p[1]);CHECK(other>=0);closed(p[1]);CHECK(read(p[0],out,4096)==4096);CHECK(!fcntl(other,F_SETFL,0));CHECK(fcntl(other,F_GETFL)==O_WRONLY);closed(other);CHECK(read(p[0],out,1)==0);closed(p[0]);
 CHECK(!pipe(p));closed(p[0]);errno=0;CHECK(write(p[1],data,1)==-1&&errno==EPIPE);closed(p[1]);
 CHECK(!pipe(p));struct kitos_pollfd f[]={{p[0],KITOS_POLLIN,0},{-1,KITOS_POLLIN,0}};uint64_t before=kitos_clock_ms();CHECK(!kitos_poll(f,2,20));CHECK(kitos_clock_ms()>=before+20);CHECK(write(p[1],data,1)==1);CHECK(kitos_poll(f,2,0)==1&&(f[0].revents&KITOS_POLLIN));closed(p[1]);CHECK(read(p[0],out,1)==1);CHECK(kitos_poll(f,2,0)==1&&(f[0].revents&KITOS_POLLHUP));closed(p[0]);CHECK(kitos_poll(f,2,0)==1&&(f[0].revents&KITOS_POLLNVAL));
 CHECK(!kitos_channel(p,0));CHECK(write(p[0],"message",7)==7);errno=0;CHECK(read(p[1],out,6)==-1&&errno==EMSGSIZE);CHECK(read(p[1],out,7)==7&&!memcmp(out,"message",7));CHECK(write(p[1],"response",8)==8);CHECK(read(p[0],out,4096)==8);closed(p[0]);CHECK(read(p[1],out,1)==0);closed(p[1]);
 fd=kitos_listen("ipc-probe",2,0);CHECK(fd>=0);maps[0]=(struct kitos_fd_map){fd,3};pid=child("server",maps,1);other=kitos_connect("ipc-probe",0);CHECK(other>=0);CHECK(write(other,"hello",5)==5);CHECK(read(other,out,4096)==5&&!memcmp(out,"reply",5));closed(other);waited(pid,0);closed(fd);
 fd=kitos_event_create(0,KITOS_IPC_NONBLOCK);CHECK(fd>=0);v=9;CHECK(write(fd,&v,8)==8);CHECK(read(fd,&v,8)==8&&v==9);errno=0;CHECK(read(fd,&v,8)==-1&&errno==EAGAIN);closed(fd);
 fd=kitos_shm_open("ipc-probe",8192,KITOS_IPC_CREATE|KITOS_IPC_EXCL|KITOS_IPC_CLOEXEC);CHECK(fd>=0);volatile uint32_t *shared=kitos_shm_map(fd,0);CHECK(shared!=(void *)-1&&shared[0]==0);shared[0]=123;maps[0]=(struct kitos_fd_map){fd,3};pid=child("shared",maps,1);waited(pid,0);CHECK(shared[1024]==456);pid=child("readonly",maps,1);int status;CHECK(kitos_waitpid(pid,&status,0)==pid);CHECK(WIFSIGNALED(status)&&WTERMSIG(status)==11);CHECK(shared[0]==123);
 other=kitos_shm_open("ipc-probe",0,KITOS_IPC_READONLY);CHECK(other>=0);errno=0;CHECK(kitos_shm_map(other,0)==(void *)-1&&errno==EACCES);void *ro=kitos_shm_map(other,KITOS_IPC_READONLY);CHECK(ro!=(void *)-1);closed(other);CHECK(!kitos_shm_unlink("ipc-probe"));closed(fd);CHECK(shared[0]==123);CHECK(!kitos_shm_unmap(ro));CHECK(!kitos_shm_unmap((void *)shared));errno=0;CHECK(kitos_shm_unmap((void *)shared)==-1&&errno==EINVAL);
 /* Closing/exit must wake and reclaim even a process parked inside a syscall. */
 for(unsigned i=0;i<8;++i){CHECK(!pipe(p));maps[0]=(struct kitos_fd_map){p[0],0};pid=child(i&1?"block-poll":"block-read",maps,1);closed(p[0]);CHECK(!kitos_sleep_ms(5));CHECK(!kill(pid,9));CHECK(kitos_waitpid(pid,&status,0)==pid);errno=0;CHECK(write(p[1],data,1)==-1&&errno==EPIPE);closed(p[1]);}
 CHECK(!pipe(p));maps[0]=(struct kitos_fd_map){p[1],1};pid=child("writer",maps,1);closed(p[1]);CHECK(!kitos_sleep_ms(20));CHECK(!kill(pid,9));CHECK(kitos_waitpid(pid,&status,0)==pid);closed(p[0]);
 fd=kitos_listen("ipc-block",1,0);CHECK(fd>=0);maps[0]=(struct kitos_fd_map){fd,3};pid=child("block-accept",maps,1);closed(fd);CHECK(!kitos_sleep_ms(5));CHECK(!kill(pid,9));CHECK(kitos_waitpid(pid,&status,0)==pid);errno=0;CHECK(kitos_connect("ipc-block",0)<0&&errno==ENOENT);
 fd=open("/tmp/ipc-fd",O_CREAT|O_TRUNC|O_RDWR,0666);CHECK(fd>=0);CHECK(write(fd,"abcdef",6)==6);CHECK(lseek(fd,0,SEEK_SET)==0);other=dup(fd);CHECK(other>=0);CHECK(read(other,out,2)==2&&!memcmp(out,"ab",2));closed(fd);CHECK(read(other,out,2)==2&&!memcmp(out,"cd",2));closed(other);CHECK(!unlink("/tmp/ipc-fd"));
 /* Default CLOEXEC stdio is absent, explicit mapping clears CLOEXEC. */
 fd=dup(0);CHECK(fd>=0);CHECK(!fcntl(0,F_SETFD,FD_CLOEXEC));pid=child("cloexec",NULL,0);waited(pid,0);CHECK(dup2(fd,0)==0);closed(fd);
 int fds[16],used=0;while((other=dup(1))>=0)fds[used++]=other;CHECK(errno==EMFILE);CHECK(pipe(p)<0&&errno==EMFILE);closed(fds[--used]);CHECK(pipe(p)<0&&errno==EMFILE);other=dup(1);CHECK(other>=0);closed(other);for(int i=0;i<used;++i)closed(fds[i]);
 errno=0;CHECK(dup2(1,-1)<0&&errno==EBADF);
 puts("[ipc] pipes/backpressure/EOF/dup/poll/messages/names/events/shared/readonly/kill/reclaim PASS");return 0;
}
