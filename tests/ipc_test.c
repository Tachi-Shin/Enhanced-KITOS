#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <kernel/ipc.h>
#include <kernel/error.h>
#include <kernel/page.h>
uint64_t test_irq_mask;
static void *pages[18000];static unsigned int live,checks;static int budget=-1;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);abort();}}while(0)
uint64_t page_alloc(void){if(budget==0)return 0;if(budget>0)--budget;for(unsigned i=0;i<18000;++i)if(!pages[i]){pages[i]=malloc(4096);CHECK(pages[i]);++live;return (uintptr_t)pages[i];}abort();}
int page_free(uint64_t p){for(unsigned i=0;i<18000;++i)if(pages[i]==(void *)(uintptr_t)p){free(pages[i]);pages[i]=NULL;--live;return 0;}abort();}
void task_event_wake(volatile uint64_t *e){CHECK(e==&ipc_epoch);}
int main(void){
 uint32_t p[2],h,a,b,l;unsigned n;char data[4096],out[4096];memset(data,0xab,sizeof(data));
 CHECK(!ipc_pair(IPC_PIPE,0,p));CHECK(ipc_read(p[0],out,1,&n)==-K_EAGAIN);CHECK(ipc_write(p[0],data,1,&n)==-K_EBADF);
 CHECK(!ipc_write(p[1],data,4096,&n)&&n==4096);CHECK(!ipc_poll(p[1]));CHECK(ipc_write(p[1],data,1,&n)==-K_EAGAIN);
 CHECK(!ipc_read(p[0],out,1023,&n)&&n==1023);CHECK(ipc_write(p[1],data,1024,&n)==-K_EAGAIN);CHECK(!ipc_write(p[1],data,1023,&n));
 CHECK(!ipc_read(p[0],out,4096,&n)&&n==4096&&!memcmp(data,out,4096));CHECK(!ipc_retain(p[1]));CHECK(!ipc_close(p[1]));CHECK(ipc_read(p[0],out,1,&n)==-K_EAGAIN);
 CHECK(!ipc_close(p[1]));CHECK(ipc_poll(p[0])==(KITOS_POLLIN|KITOS_POLLHUP));CHECK(!ipc_read(p[0],out,1,&n)&&n==0);h=p[0];CHECK(!ipc_close(h));CHECK(!live);CHECK(ipc_close(h)==-K_EBADF);
 CHECK(!ipc_pair(IPC_PIPE,0,p));CHECK(p[0]!=h);CHECK(!ipc_close(p[0]));CHECK(ipc_write(p[1],data,1,&n)==-K_EPIPE);CHECK(ipc_poll(p[1])==KITOS_POLLERR);CHECK(!ipc_close(p[1]));
 CHECK(!ipc_pair(IPC_CHANNEL,KITOS_IPC_NONBLOCK,p));for(unsigned i=0;i<4;++i)CHECK(!ipc_write(p[0],data,4096,&n));CHECK(ipc_write(p[0],data,1,&n)==-K_EAGAIN);
 CHECK(ipc_read(p[1],out,4095,&n)==-K_EMSGSIZE);CHECK(!ipc_read(p[1],out,4096,&n)&&n==4096);CHECK(!ipc_write(p[1],"reply",5,&n));CHECK(!ipc_read(p[0],out,9,&n)&&n==5&&!memcmp(out,"reply",5));
 CHECK(!ipc_close(p[0]));for(unsigned i=0;i<3;++i)CHECK(!ipc_read(p[1],out,4096,&n)&&n==4096);CHECK(!ipc_read(p[1],out,1,&n)&&!n);CHECK(!ipc_close(p[1]));CHECK(!live);
 CHECK(!ipc_listen("server",1,&l));CHECK(ipc_listen("server",1,&h)==-K_EEXIST);CHECK(ipc_accept(l,0,&h)==-K_EAGAIN);CHECK(!ipc_connect("server",0,&a));CHECK(ipc_connect("server",0,&h)==-K_EAGAIN);CHECK(ipc_poll(l)==KITOS_POLLIN);CHECK(!ipc_accept(l,KITOS_IPC_NONBLOCK,&b));CHECK(ipc_get_flags(b)==KITOS_IPC_NONBLOCK);CHECK(!ipc_close(b));CHECK(!ipc_close(a));CHECK(!ipc_connect("server",0,&a));CHECK(!ipc_close(l));CHECK(ipc_poll(a)&KITOS_POLLHUP);CHECK(!ipc_close(a));CHECK(ipc_connect("server",0,&h)==-K_ENOENT);CHECK(!live);
 CHECK(!ipc_event(0,0,&h));uint64_t v=5;CHECK(ipc_read(h,&v,8,&n)==-K_EAGAIN);CHECK(!ipc_write(h,&v,8,&n));CHECK(!ipc_read(h,&v,8,&n)&&v==5);v=~0ULL;CHECK(ipc_write(h,&v,8,&n)==-K_EINVAL);v=~1ULL;CHECK(!ipc_write(h,&v,8,&n));v=1;CHECK(ipc_write(h,&v,8,&n)==-K_EAGAIN);CHECK(ipc_poll(h)==KITOS_POLLIN);CHECK(!ipc_close(h));
 CHECK(!ipc_event(2,KITOS_EVENT_SEMAPHORE,&h));CHECK(!ipc_read(h,&v,8,&n)&&v==1);CHECK(!ipc_read(h,&v,8,&n)&&v==1);CHECK(ipc_read(h,&v,8,&n)==-K_EAGAIN);CHECK(!ipc_close(h));
 CHECK(!ipc_shm_open("shared",4097,KITOS_IPC_CREATE|KITOS_IPC_EXCL,&a));CHECK(live==3);CHECK(!ipc_shm_open("shared",0,KITOS_IPC_READONLY,&b));CHECK(ipc_shm_open("shared",4097,KITOS_IPC_CREATE|KITOS_IPC_EXCL,&h)==-K_EEXIST);
 CHECK(ipc_size(a)==4097);CHECK(ipc_shm_page(a,0)&&ipc_shm_page(a,1)&&!ipc_shm_page(a,2));memcpy((void *)(uintptr_t)ipc_shm_page(a,0),data,4096);CHECK(!ipc_shm_read(b,4094,out,3)&&out[0]==(char)0xab&&out[2]==0);CHECK(ipc_shm_read(b,4096,out,2)==-K_EINVAL);CHECK(!ipc_shm_unlink("shared"));CHECK(ipc_shm_open("shared",0,0,&h)==-K_ENOENT);CHECK(!ipc_close(a));CHECK(ipc_size(b)==4097);CHECK(!ipc_close(b));CHECK(!live);
 for(int fail=0;fail<9;++fail){budget=fail;int rc=ipc_pair(IPC_CHANNEL,0,p);if(!rc){ipc_close(p[0]);ipc_close(p[1]);}else CHECK(rc==-K_ENOMEM);CHECK(!live);}budget=-1;
 for(int fail=0;fail<5;++fail){budget=fail;int rc=ipc_shm_open(NULL,8193,KITOS_IPC_CREATE,&h);if(!rc)ipc_close(h);else CHECK(rc==-K_ENOMEM);CHECK(!live);}budget=-1;
 CHECK(!ipc_shm_open(NULL,KITOS_SHM_MAX,KITOS_IPC_CREATE,&h));CHECK(ipc_shm_page(h,16383));CHECK(!ipc_close(h));CHECK(!live);
 uint32_t ids[32];for(unsigned i=0;i<32;++i)CHECK(!ipc_event(i,0,&ids[i]));CHECK(ipc_event(0,0,&h)==-K_ENFILE);for(unsigned i=0;i<32;++i)CHECK(!ipc_close(ids[i]));
 for(unsigned i=0;i<10000;++i){CHECK(!ipc_pair(IPC_PIPE,0,p));CHECK(ipc_retain(ids[i%32])==-K_EBADF);CHECK(!ipc_write(p[1],data,i%4096+1,&n));CHECK(!ipc_read(p[0],out,4096,&n)&&n==i%4096+1);CHECK(!ipc_close(p[0]));CHECK(!ipc_close(p[1]));}CHECK(!live);
 printf("IPC object boundaries/ownership/EOF/messages/names/events/shared/OOM/reuse: %u checks PASS\n",checks);return 0;
}
