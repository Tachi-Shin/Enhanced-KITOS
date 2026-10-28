#include <kernel/ipc.h>
#include <kernel/error.h>
#include <kernel/page.h>
#include <kernel/task.h>
#include <arch/memory.h>
#include <arch/irq.h>
#include <lib/string.h>
#define OBJECTS 32U
#define MESSAGES 4U
#define NAME_MAX 64U
struct queue {uint64_t pages[MESSAGES];unsigned int lengths[MESSAGES],head,tail,used;};
struct object {
    unsigned int state,generation,kind,refs[2],flags[2];
    char name[NAME_MAX];
    union {
        struct queue q[2];
        struct {uint32_t pending[8];unsigned int head,tail,count,limit;} listener;
        struct {uint64_t count;bool semaphore;} event;
        struct {size_t size;uint64_t tables[32];} shm;
    } u;
};
static struct object objects[OBJECTS];
volatile uint64_t ipc_epoch;
void ipc_notify(void)
{uint64_t f=arch_irq_save();++ipc_epoch;task_event_wake(&ipc_epoch);arch_irq_restore(f);}
static uint32_t token(struct object *o,unsigned int side)
{return (o->generation<<7)|((unsigned int)(o-objects)+1)*2|side;}
static struct object *get(uint32_t id)
{
    unsigned int n=(id>>1)&63;
    if(!n||n>OBJECTS)return NULL;
    struct object *o=&objects[n-1];
    return o->state==2&&o->generation==(id>>7)&&o->refs[id&1]?o:NULL;
}
static struct object *reserve(unsigned int kind)
{
    uint64_t f=arch_irq_save();struct object *o=NULL;
    for(unsigned int i=0;i<OBJECTS;++i)if(!objects[i].state&&objects[i].generation<0x1ffffffU){
        o=&objects[i];unsigned int generation=o->generation+1;memset(o,0,sizeof(*o));
        o->generation=generation;o->kind=kind;o->state=1;break;
    }
    arch_irq_restore(f);return o;
}
static void destroy(struct object *o)
{
    if(o->kind==IPC_PIPE||o->kind==IPC_CHANNEL){
        for(unsigned int q=0;q<2;++q)for(unsigned int p=0;p<MESSAGES;++p)if(o->u.q[q].pages[p])page_free(o->u.q[q].pages[p]);
    }else if(o->kind==IPC_LISTENER){
        for(unsigned int i=0;i<o->u.listener.count;++i)ipc_close(o->u.listener.pending[(o->u.listener.tail+i)%8]);
    }else if(o->kind==IPC_SHM){
        for(unsigned int i=0;i<32;++i)if(o->u.shm.tables[i]){
            uint64_t *p=arch_phys_to_virt(o->u.shm.tables[i]);
            for(unsigned int j=0;j<512;++j)if(p[j])page_free(p[j]);
            page_free(o->u.shm.tables[i]);
        }
    }
    uint64_t f=arch_irq_save();o->state=0;arch_irq_restore(f);ipc_notify();
}
int ipc_retain(uint32_t id)
{
    uint64_t f=arch_irq_save();struct object *o=get(id);int rc=-K_EBADF;
    if(o&&o->refs[id&1]<0x7fffffffU){++o->refs[id&1];rc=0;}
    arch_irq_restore(f);return rc;
}
int ipc_close(uint32_t id)
{
    uint64_t f=arch_irq_save();struct object *o=get(id);
    if(!o){arch_irq_restore(f);return -K_EBADF;}
    --o->refs[id&1];bool dead=!o->refs[0]&&!o->refs[1];if(dead)o->state=3;
    arch_irq_restore(f);ipc_notify();if(dead)destroy(o);return 0;
}
int ipc_pair(unsigned int kind,unsigned int flags,uint32_t out[2])
{
    if(!out||(kind!=IPC_PIPE&&kind!=IPC_CHANNEL)||(flags&~KITOS_IPC_NONBLOCK))return -K_EINVAL;
    struct object *o=reserve(kind);if(!o)return -K_ENFILE;
    unsigned int queues=kind==IPC_PIPE?1:2,pages=kind==IPC_PIPE?1:MESSAGES;
    for(unsigned int q=0;q<queues;++q)for(unsigned int p=0;p<pages;++p){
        o->u.q[q].pages[p]=page_alloc();if(!o->u.q[q].pages[p]){destroy(o);return -K_ENOMEM;}
        memset(arch_phys_to_virt(o->u.q[q].pages[p]),0,PAGE_SIZE);
    }
    uint64_t f=arch_irq_save();o->refs[0]=o->refs[1]=1;o->flags[0]=o->flags[1]=flags;o->state=2;
    out[0]=token(o,0);out[1]=token(o,1);arch_irq_restore(f);return 0;
}
int ipc_read(uint32_t id,void *data,unsigned int size,unsigned int *done)
{
    if(!done||(size&&!data))return -K_EINVAL;
    *done=0;
    uint64_t f=arch_irq_save();struct object *o=get(id);int rc=0;unsigned int side=id&1;
    if(!o){rc=-K_EBADF;goto end;}
    if(o->kind==IPC_EVENT){
        if(size!=8){rc=-K_EINVAL;goto end;}
        if(!o->u.event.count){rc=-K_EAGAIN;goto end;}
        uint64_t n=o->u.event.semaphore?1:o->u.event.count;o->u.event.count-=n;memcpy(data,&n,8);*done=8;goto end;
    }
    if((o->kind!=IPC_PIPE&&o->kind!=IPC_CHANNEL)||(o->kind==IPC_PIPE&&side)){rc=-K_EBADF;goto end;}
    if(!size)goto end;
    struct queue *q=&o->u.q[o->kind==IPC_PIPE?0:side];
    if(!q->used){rc=o->refs[side^1]?-K_EAGAIN:0;goto end;}
    if(o->kind==IPC_PIPE){
        unsigned int n=size<q->used?size:q->used;uint8_t *p=arch_phys_to_virt(q->pages[0]);
        for(unsigned int i=0;i<n;++i)((uint8_t *)data)[i]=p[(q->tail+i)%PAGE_SIZE];
        q->tail=(q->tail+n)%PAGE_SIZE;q->used-=n;*done=n;
    }else{
        unsigned int n=q->lengths[q->tail%MESSAGES];if(size<n){rc=-K_EMSGSIZE;goto end;}
        memcpy(data,arch_phys_to_virt(q->pages[q->tail%MESSAGES]),n);++q->tail;--q->used;*done=n;
    }
end:arch_irq_restore(f);if(!rc&&*done)ipc_notify();return rc;
}
int ipc_write(uint32_t id,const void *data,unsigned int size,unsigned int *done)
{
    if(!done||(size&&!data))return -K_EINVAL;
    *done=0;
    uint64_t f=arch_irq_save();struct object *o=get(id);int rc=0;unsigned int side=id&1;
    if(!o){rc=-K_EBADF;goto end;}
    if(o->kind==IPC_EVENT){
        if(size!=8){rc=-K_EINVAL;goto end;}uint64_t n;memcpy(&n,data,8);
        if(n==~0ULL){rc=-K_EINVAL;goto end;}
        if(n>~1ULL-o->u.event.count){rc=-K_EAGAIN;goto end;}o->u.event.count+=n;*done=8;goto end;
    }
    if((o->kind!=IPC_PIPE&&o->kind!=IPC_CHANNEL)||(o->kind==IPC_PIPE&&!side)){rc=-K_EBADF;goto end;}
    if(!size)goto end;
    if(size>PAGE_SIZE){rc=-K_EMSGSIZE;goto end;}
    if(!o->refs[side^1]){rc=-K_EPIPE;goto end;}
    struct queue *q=&o->u.q[o->kind==IPC_PIPE?0:side^1];
    if(o->kind==IPC_PIPE){
        if(size>PAGE_SIZE-q->used){rc=-K_EAGAIN;goto end;}
        uint8_t *p=arch_phys_to_virt(q->pages[0]);for(unsigned int i=0;i<size;++i)p[(q->head+i)%PAGE_SIZE]=((const uint8_t *)data)[i];
        q->head=(q->head+size)%PAGE_SIZE;q->used+=size;*done=size;
    }else{
        if(q->used==MESSAGES){rc=-K_EAGAIN;goto end;}
        memcpy(arch_phys_to_virt(q->pages[q->head%MESSAGES]),data,size);q->lengths[q->head%MESSAGES]=size;++q->head;++q->used;*done=size;
    }
end:arch_irq_restore(f);if(!rc&&*done)ipc_notify();return rc;
}
unsigned int ipc_poll(uint32_t id)
{
    uint64_t f=arch_irq_save();struct object *o=get(id);unsigned int ready=0,s=id&1;
    if(!o){ready=KITOS_POLLNVAL;goto end;}
    if(o->kind==IPC_EVENT){if(o->u.event.count)ready|=KITOS_POLLIN;if(o->u.event.count<~1ULL)ready|=KITOS_POLLOUT;}
    else if(o->kind==IPC_LISTENER){if(o->u.listener.count)ready=KITOS_POLLIN;}
    else if(o->kind==IPC_PIPE){
        if(!s){if(o->u.q[0].used||!o->refs[1])ready|=KITOS_POLLIN;if(!o->refs[1])ready|=KITOS_POLLHUP;}
        else if(!o->refs[0])ready=KITOS_POLLERR;else if(o->u.q[0].used<PAGE_SIZE)ready=KITOS_POLLOUT;
    }else if(o->kind==IPC_CHANNEL){
        if(o->u.q[s].used||!o->refs[s^1])ready|=KITOS_POLLIN;
        if(!o->refs[s^1])ready|=KITOS_POLLHUP|KITOS_POLLERR;
        else if(o->u.q[s^1].used<MESSAGES)ready|=KITOS_POLLOUT;
    }
end:arch_irq_restore(f);return ready;
}
int ipc_kind(uint32_t id)
{uint64_t f=arch_irq_save();struct object *o=get(id);int n=o?(int)o->kind:-K_EBADF;arch_irq_restore(f);return n;}
int ipc_get_flags(uint32_t id)
{uint64_t f=arch_irq_save();struct object *o=get(id);int n=o?(int)o->flags[id&1]:-K_EBADF;arch_irq_restore(f);return n;}
int ipc_set_flags(uint32_t id,unsigned int flags)
{
    if(flags&~KITOS_IPC_NONBLOCK)return -K_EINVAL;
    uint64_t f=arch_irq_save();struct object *o=get(id);if(o)o->flags[id&1]=flags;arch_irq_restore(f);return o?0:-K_EBADF;
}
static bool valid_name(const char *name)
{if(!name||!*name)return false;size_t n=0;while(name[n]&&n<NAME_MAX)++n;return n<NAME_MAX;}
static struct object *named(const char *name,unsigned int kind)
{for(unsigned int i=0;i<OBJECTS;++i)if(objects[i].state==2&&objects[i].kind==kind&&!strcmp(objects[i].name,name))return &objects[i];return NULL;}
int ipc_listen(const char *name,unsigned int backlog,uint32_t *out)
{
    if(!valid_name(name)||!backlog||backlog>8||!out)return -K_EINVAL;
    struct object *o=reserve(IPC_LISTENER);if(!o)return -K_ENFILE;
    uint64_t f=arch_irq_save();if(named(name,IPC_LISTENER)){arch_irq_restore(f);destroy(o);return -K_EEXIST;}
    strcpy(o->name,name);o->u.listener.limit=backlog;o->refs[0]=1;o->state=2;*out=token(o,0);arch_irq_restore(f);ipc_notify();return 0;
}
int ipc_connect(const char *name,unsigned int flags,uint32_t *out)
{
    if(!valid_name(name)||!out||(flags&~KITOS_IPC_NONBLOCK))return -K_EINVAL;
    uint64_t before=arch_irq_save();struct object *listener=named(name,IPC_LISTENER);
    int check=!listener?-K_ENOENT:listener->u.listener.count==listener->u.listener.limit?-K_EAGAIN:0;
    arch_irq_restore(before);if(check)return check;
    uint32_t pair[2];int rc=ipc_pair(IPC_CHANNEL,flags,pair);if(rc)return rc;
    uint64_t f=arch_irq_save();struct object *o=named(name,IPC_LISTENER);
    if(!o)rc=-K_ENOENT;else if(o->u.listener.count==o->u.listener.limit)rc=-K_EAGAIN;
    else{o->u.listener.pending[o->u.listener.head++%8]=pair[0];++o->u.listener.count;*out=pair[1];}
    arch_irq_restore(f);if(rc){ipc_close(pair[0]);ipc_close(pair[1]);}else ipc_notify();return rc;
}
int ipc_accept(uint32_t id,unsigned int flags,uint32_t *out)
{
    if(!out||(flags&~KITOS_IPC_NONBLOCK))return -K_EINVAL;
    uint64_t f=arch_irq_save();struct object *o=get(id);int rc=0;
    if(!o||o->kind!=IPC_LISTENER)rc=-K_EBADF;else if(!o->u.listener.count)rc=-K_EAGAIN;
    else{*out=o->u.listener.pending[o->u.listener.tail++%8];--o->u.listener.count;struct object *c=get(*out);c->flags[0]=flags;}
    arch_irq_restore(f);if(!rc)ipc_notify();return rc;
}
int ipc_event(uint64_t initial,unsigned int flags,uint32_t *out)
{
    if(!out||initial==~0ULL||(flags&~(KITOS_EVENT_SEMAPHORE|KITOS_IPC_NONBLOCK)))return -K_EINVAL;
    struct object *o=reserve(IPC_EVENT);if(!o)return -K_ENFILE;
    o->u.event.count=initial;o->u.event.semaphore=(flags&KITOS_EVENT_SEMAPHORE)!=0;o->flags[0]=flags&KITOS_IPC_NONBLOCK;
    o->refs[0]=1;o->state=2;*out=token(o,0);return 0;
}
int ipc_shm_open(const char *name,size_t size,unsigned int flags,uint32_t *out)
{
    if(!out||(name&&!valid_name(name))||size>KITOS_SHM_MAX||(flags&~(KITOS_IPC_CREATE|KITOS_IPC_EXCL|KITOS_IPC_READONLY)))return -K_EINVAL;
    uint64_t f=arch_irq_save();struct object *o=name?named(name,IPC_SHM):NULL;
    if(o){
        int rc=0;if((flags&(KITOS_IPC_CREATE|KITOS_IPC_EXCL))==(KITOS_IPC_CREATE|KITOS_IPC_EXCL))rc=-K_EEXIST;
        else if(size&&size!=o->u.shm.size)rc=-K_EINVAL;else{++o->refs[0];*out=token(o,0);}
        arch_irq_restore(f);return rc;
    }
    arch_irq_restore(f);
    if(!(flags&KITOS_IPC_CREATE))return -K_ENOENT;
    if(!size)return -K_EINVAL;
    o=reserve(IPC_SHM);if(!o)return -K_ENFILE;
    o->u.shm.size=size;unsigned int pages=(unsigned int)((size+PAGE_SIZE-1)/PAGE_SIZE);
    for(unsigned int i=0;i<pages;++i){
        unsigned int t=i/512;if(!o->u.shm.tables[t]){
            o->u.shm.tables[t]=page_alloc();if(!o->u.shm.tables[t]){destroy(o);return -K_ENOMEM;}
            memset(arch_phys_to_virt(o->u.shm.tables[t]),0,PAGE_SIZE);
        }
        uint64_t *table=arch_phys_to_virt(o->u.shm.tables[t]);table[i%512]=page_alloc();
        if(!table[i%512]){destroy(o);return -K_ENOMEM;}memset(arch_phys_to_virt(table[i%512]),0,PAGE_SIZE);
    }
    f=arch_irq_save();if(name&&named(name,IPC_SHM)){arch_irq_restore(f);destroy(o);return ipc_shm_open(name,size,flags,out);}
    if(name)strcpy(o->name,name);
    o->refs[0]=1;o->state=2;*out=token(o,0);arch_irq_restore(f);return 0;
}
int ipc_shm_unlink(const char *name)
{
    if(!valid_name(name))return -K_EINVAL;
    uint64_t f=arch_irq_save();struct object *o=named(name,IPC_SHM);if(o)o->name[0]=0;arch_irq_restore(f);return o?0:-K_ENOENT;
}
uint64_t ipc_size(uint32_t id)
{uint64_t f=arch_irq_save();struct object *o=get(id);uint64_t n=o&&o->kind==IPC_SHM?o->u.shm.size:0;arch_irq_restore(f);return n;}
uint64_t ipc_shm_page(uint32_t id,unsigned int index)
{
    uint64_t f=arch_irq_save();struct object *o=get(id);uint64_t p=0;
    if(o&&o->kind==IPC_SHM&&(uint64_t)index*PAGE_SIZE<o->u.shm.size){uint64_t *t=arch_phys_to_virt(o->u.shm.tables[index/512]);p=t[index%512];}
    arch_irq_restore(f);return p;
}
int ipc_shm_read(uint32_t id,uint64_t offset,void *out,unsigned int bytes)
{
    uint64_t size=ipc_size(id);if(!size||offset>size||bytes>size-offset)return -K_EINVAL;
    for(unsigned int done=0;done<bytes;){
        uint64_t pos=offset+done,physical=ipc_shm_page(id,(unsigned int)(pos/PAGE_SIZE));if(!physical)return -K_EBADF;
        unsigned int n=PAGE_SIZE-pos%PAGE_SIZE;if(n>bytes-done)n=bytes-done;
        memcpy((uint8_t *)out+done,(uint8_t *)arch_phys_to_virt(physical)+pos%PAGE_SIZE,n);done+=n;
    }
    return 0;
}
