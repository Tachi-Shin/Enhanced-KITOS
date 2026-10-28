#include "internal.h"
#include <kernel/ipc.h>
#include <kernel/error.h>
#include <kernel/clock.h>
#include <kernel/console.h>
#include <kernel/task.h>
#include <uapi/syscall.h>
#include <arch/irq.h>
#include <lib/string.h>
#define SHARED_BASE 0x4000000000ULL
#define SHARED_STRIDE 0x4000000ULL
int process_fd_close(struct process *p,unsigned int index)
{
    if(index>=PROCESS_FD_MAX||!p->fd[index].kind)return -K_EBADF;
    struct process_fd *fd=&p->fd[index];int rc=0;
    if(fd->kind==FD_FILE)rc=vfs_close(fd->file);else if(fd->kind==FD_IPC)rc=ipc_close(fd->handle);
    if(rc!=-K_EBUSY)*fd=(struct process_fd){0};
    return rc;
}
int process_fd_copy(struct process_fd *out,const struct process_fd *in)
{
    if(!in->kind)return -K_EBADF;
    int rc=in->kind==FD_FILE?vfs_retain(in->file):in->kind==FD_IPC?ipc_retain(in->handle):0;
    if(!rc)*out=*in;
    return rc;
}
void process_ipc_cleanup(struct process *p)
{
    /* The task is dead, so its borrowed mappings can no longer be accessed.
     * VM destruction ignores borrowed physical pages and may run separately. */
    for(unsigned int i=0;i<PROCESS_MAPS_MAX;++i)if(p->mappings[i].handle){
        ipc_close(p->mappings[i].handle);p->mappings[i]=(struct process_mapping){0};
    }
}
static int free_fd(struct process *p)
{for(unsigned int i=0;i<PROCESS_FD_MAX;++i)if(!p->fd[i].kind)return (int)i;return -K_EMFILE;}
static int install(struct process *p,uint32_t handle,unsigned int flags,bool readonly)
{
    int fd=free_fd(p);if(fd<0){ipc_close(handle);return fd;}
    p->fd[fd]=(struct process_fd){.kind=FD_IPC,.flags=flags&KITOS_IPC_CLOEXEC,.handle=handle,.readonly=readonly};return fd;
}
static int wait_event(uint64_t seen,uint64_t deadline)
{
    process_unpin();int rc=task_event_wait(&ipc_epoch,seen,deadline);process_pin();return rc;
}
int64_t process_ipc_io(struct process_fd *fd,uint64_t address,uint64_t length,bool write)
{
    if(length>1024U*1024)return -K_EINVAL;
    int kind=ipc_kind(fd->handle);if(kind<0)return kind;
    if(kind==IPC_SHM||kind==IPC_LISTENER)return -K_EBADF;
    if(kind==IPC_CHANNEL&&write&&length>KITOS_MESSAGE_MAX)return -K_EMSGSIZE;
    if(kind==IPC_EVENT&&length!=8)return -K_EINVAL;
    if(!length)return 0;
    struct vm_space *space=task_current_space();
    if(vm_user_check(space,address,(size_t)length,write?VM_READ:VM_WRITE))return -K_EFAULT;
    char buffer[KITOS_PIPE_BUF];uint64_t total=0;
    while(total<length){
        unsigned int n=length-total>sizeof(buffer)?sizeof(buffer):(unsigned int)(length-total),done=0;
        if(write&&copy_from_user(space,buffer,address+total,n))return total?(int64_t)total:-K_EFAULT;
        for(;;){
            uint64_t seen=ipc_epoch;
            int rc=write?ipc_write(fd->handle,buffer,n,&done):ipc_read(fd->handle,buffer,n,&done);
            if(rc==-K_EAGAIN&&!(ipc_get_flags(fd->handle)&KITOS_IPC_NONBLOCK)){
                rc=wait_event(seen,0);if(!rc)continue;
            }
            if(rc)return total?(int64_t)total:rc;
            break;
        }
        if(!write&&done&&copy_to_user(space,address+total,buffer,done))return total?(int64_t)total:-K_EFAULT;
        total+=done;if(!write||kind!=IPC_PIPE||!done)break;
    }
    return (int64_t)total;
}
static unsigned int ready(const struct process_fd *fd)
{
    if(!fd->kind)return KITOS_POLLNVAL;
    if(fd->kind==FD_IPC)return ipc_poll(fd->handle);
    if(fd->kind==FD_INPUT)return console_input_ready()?KITOS_POLLIN:0;
    if(fd->kind==FD_OUTPUT)return KITOS_POLLOUT;
    return ((fd->flags&VFS_READ)?KITOS_POLLIN:0)|((fd->flags&VFS_WRITE)?KITOS_POLLOUT:0);
}
static int poll_fds(struct process *p,uint64_t address,uint64_t count,int64_t timeout)
{
    if(count>PROCESS_FD_MAX||timeout< -1||timeout>0x7fffffff)return -K_EINVAL;
    size_t bytes=(size_t)count*sizeof(struct kitos_pollfd);struct kitos_pollfd fds[PROCESS_FD_MAX];
    struct vm_space *space=task_current_space();
    if(bytes&&(vm_user_check(space,address,bytes,VM_WRITE)||copy_from_user(space,fds,address,bytes)))return -K_EFAULT;
    uint64_t deadline=timeout<0?0:clock_ms()+(uint64_t)timeout;
    for(;;){
        uint64_t seen=ipc_epoch;int n=0;
        for(unsigned int i=0;i<count;++i){
            unsigned int r=fds[i].fd<0?0:fds[i].fd>=(int)PROCESS_FD_MAX?KITOS_POLLNVAL:ready(&p->fd[fds[i].fd]);
            fds[i].revents=r&(fds[i].events|KITOS_POLLERR|KITOS_POLLHUP|KITOS_POLLNVAL);if(fds[i].revents)++n;
        }
        if(n||timeout==0||(deadline&&clock_ms()>=deadline)){
            if(bytes&&copy_to_user(space,address,fds,bytes))return -K_EFAULT;
            return n;
        }
        int rc=wait_event(seen,deadline);if(rc&&rc!=-K_ETIMEDOUT)return rc;
    }
}
static int64_t map_shared(struct process *p,unsigned int fd,unsigned int flags)
{
    if(fd>=PROCESS_FD_MAX||p->fd[fd].kind!=FD_IPC||ipc_kind(p->fd[fd].handle)!=IPC_SHM)return -K_EBADF;
    if(flags&~KITOS_IPC_READONLY)return -K_EINVAL;
    if(p->fd[fd].readonly&&!(flags&KITOS_IPC_READONLY))return -K_EACCES;
    unsigned int i;for(i=0;i<PROCESS_MAPS_MAX;++i)if(!p->mappings[i].handle)break;
    if(i==PROCESS_MAPS_MAX)return -K_ENOSPC;
    uint32_t h=p->fd[fd].handle;uint64_t size=ipc_size(h),base=SHARED_BASE+i*SHARED_STRIDE;
    int rc=ipc_retain(h);if(rc)return rc;
    struct vm_space *space=task_current_space();uint64_t off=0;
    for(;off<size;off+=PAGE_SIZE){
        rc=vm_map_shared(space,base+off,ipc_shm_page(h,(unsigned int)(off/PAGE_SIZE)),!(flags&KITOS_IPC_READONLY));if(rc)break;
    }
    if(rc){while(off){off-=PAGE_SIZE;vm_unmap(space,base+off);}ipc_close(h);return rc;}
    p->mappings[i]=(struct process_mapping){base,size,h};return (int64_t)base;
}
int64_t process_ipc_call(struct process *p,uint64_t number,const uint64_t a[6])
{
    unsigned int flags=(unsigned int)a[1];int rc;uint32_t h=0;
    if(number==KITOS_SYS_DISPLAY){
        if(a[0]>0xffffffffU)return -K_EINVAL;
        if(a[0]==KITOS_DISPLAY_PRESENT&&(a[2]>0xffffffffU||a[3]>0xffffffffU||a[4]>0xffffffffU))return -K_EINVAL;
        uint32_t source=0;
        if(a[0]==KITOS_DISPLAY_PRESENT){if(a[1]>=PROCESS_FD_MAX||p->fd[a[1]].kind!=FD_IPC||ipc_kind(p->fd[a[1]].handle)!=IPC_SHM)return -K_EBADF;source=p->fd[a[1]].handle;}
        return console_graphics(p->pid,(unsigned int)a[0],source,(unsigned int)a[2],(unsigned int)a[3],(unsigned int)a[4]);
    }
    if(number==KITOS_SYS_POLL)return poll_fds(p,a[0],a[1],(int64_t)a[2]);
    if(number==KITOS_SYS_PIPE||number==KITOS_SYS_CHANNEL){
        if(a[1]&~(uint64_t)(KITOS_IPC_NONBLOCK|KITOS_IPC_CLOEXEC))return -K_EINVAL;
        if(vm_user_check(task_current_space(),a[0],2*sizeof(int),VM_WRITE))return -K_EFAULT;
        uint32_t pair[2];rc=ipc_pair(number==KITOS_SYS_PIPE?IPC_PIPE:IPC_CHANNEL,flags&KITOS_IPC_NONBLOCK,pair);if(rc)return rc;
        int fds[2];fds[0]=install(p,pair[0],flags,false);if(fds[0]<0){ipc_close(pair[1]);return fds[0];}
        fds[1]=install(p,pair[1],flags,false);if(fds[1]<0){process_fd_close(p,(unsigned int)fds[0]);return fds[1];}
        if(copy_to_user(task_current_space(),a[0],fds,sizeof(fds))){process_fd_close(p,fds[0]);process_fd_close(p,fds[1]);return -K_EFAULT;}return 0;
    }
    if(number==KITOS_SYS_DUP){
        if(a[0]>=PROCESS_FD_MAX||!p->fd[a[0]].kind)return -K_EBADF;
        int target=(int64_t)a[1]==-1?free_fd(p):a[1]>=PROCESS_FD_MAX?-K_EBADF:(int)a[1];if(target<0)return target;
        if(target==(int)a[0])return target;
        struct process_fd copy;do{rc=process_fd_copy(&copy,&p->fd[a[0]]);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);if(rc)return rc;
        if(p->fd[target].kind){do{rc=process_fd_close(p,target);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);}
        copy.flags&=~KITOS_IPC_CLOEXEC;p->fd[target]=copy;return target;
    }
    if(number==KITOS_SYS_FCNTL){
        if(a[0]>=PROCESS_FD_MAX||!p->fd[a[0]].kind)return -K_EBADF;
        struct process_fd *f=&p->fd[a[0]];
        if(a[1]==KITOS_F_GETFD)return f->flags&KITOS_IPC_CLOEXEC;
        if(a[1]==KITOS_F_SETFD){if(a[2]&~(uint64_t)KITOS_IPC_CLOEXEC)return -K_EINVAL;f->flags=(f->flags&~KITOS_IPC_CLOEXEC)|(unsigned int)a[2];return 0;}
        if(a[1]==KITOS_F_GETFL){
            unsigned int access=f->kind==FD_OUTPUT?1:f->kind==FD_INPUT?0:f->kind==FD_FILE?((f->flags&VFS_WRITE)?((f->flags&VFS_READ)?2:1):0):
                ipc_kind(f->handle)==IPC_PIPE?(f->handle&1):ipc_kind(f->handle)==IPC_SHM?(f->readonly?0:2):2;
            return access|(f->kind==FD_IPC?ipc_get_flags(f->handle):0);
        }
        if(a[1]==KITOS_F_SETFL)return f->kind==FD_IPC?ipc_set_flags(f->handle,(unsigned int)a[2]):-K_ENOTSUP;
        return -K_EINVAL;
    }
    if(number==KITOS_SYS_SHM_MAP){if(a[0]>0xffffffffU||a[1]>0xffffffffU)return -K_EINVAL;return map_shared(p,(unsigned int)a[0],flags);}
    if(number==KITOS_SYS_SHM_UNMAP){
        for(unsigned int i=0;i<PROCESS_MAPS_MAX;++i)if(p->mappings[i].handle&&p->mappings[i].address==a[0]){
            struct process_mapping m=p->mappings[i];
            for(uint64_t off=0;off<m.size;off+=PAGE_SIZE)vm_unmap(task_current_space(),m.address+off);
            p->mappings[i]=(struct process_mapping){0};ipc_close(m.handle);return 0;
        }
        return -K_EINVAL;
    }
    if(number==KITOS_SYS_EVENT){
        if(a[1]&~(uint64_t)(KITOS_EVENT_SEMAPHORE|KITOS_IPC_NONBLOCK|KITOS_IPC_CLOEXEC))return -K_EINVAL;
        rc=ipc_event(a[0],flags&~KITOS_IPC_CLOEXEC,&h);return rc?rc:install(p,h,flags,false);
    }
    if(number==KITOS_SYS_ACCEPT){
        if(a[0]>=PROCESS_FD_MAX||p->fd[a[0]].kind!=FD_IPC||ipc_kind(p->fd[a[0]].handle)!=IPC_LISTENER)return -K_EBADF;
        if(a[1]&~(uint64_t)(KITOS_IPC_NONBLOCK|KITOS_IPC_CLOEXEC))return -K_EINVAL;
        if(free_fd(p)<0)return -K_EMFILE;
        for(;;){uint64_t seen=ipc_epoch;rc=ipc_accept(p->fd[a[0]].handle,flags&KITOS_IPC_NONBLOCK,&h);
            if(rc!=-K_EAGAIN||(ipc_get_flags(p->fd[a[0]].handle)&KITOS_IPC_NONBLOCK))break;
            rc=wait_event(seen,0);if(rc)return rc;
        }
        return rc?rc:install(p,h,flags,false);
    }
    if(number==KITOS_SYS_LISTEN||number==KITOS_SYS_CONNECT||number==KITOS_SYS_SHM_OPEN||number==KITOS_SYS_SHM_UNLINK){
        char name[64];const char *n=name;
        if(!a[0]&&number==KITOS_SYS_SHM_OPEN)n=NULL;else{rc=process_user_string(a[0],name,sizeof(name));if(rc)return rc;}
        if(number==KITOS_SYS_SHM_UNLINK)return ipc_shm_unlink(n);
        if(free_fd(p)<0)return -K_EMFILE;
        bool ro=false;
        if(number==KITOS_SYS_LISTEN){
            if(a[1]>8||a[2]&~(uint64_t)(KITOS_IPC_NONBLOCK|KITOS_IPC_CLOEXEC))return -K_EINVAL;
            flags=(unsigned int)a[2];rc=ipc_listen(n,(unsigned int)a[1],&h);if(!rc)ipc_set_flags(h,flags&KITOS_IPC_NONBLOCK);
        }else if(number==KITOS_SYS_CONNECT){
            if(a[1]&~(uint64_t)(KITOS_IPC_NONBLOCK|KITOS_IPC_CLOEXEC))return -K_EINVAL;
            for(;;){uint64_t seen=ipc_epoch;rc=ipc_connect(n,flags&KITOS_IPC_NONBLOCK,&h);
                if(rc!=-K_EAGAIN||(flags&KITOS_IPC_NONBLOCK))break;
                rc=wait_event(seen,0);if(rc)return rc;
            }
        }else{
            if(a[1]>KITOS_SHM_MAX||a[2]&~(uint64_t)(KITOS_IPC_CREATE|KITOS_IPC_EXCL|KITOS_IPC_READONLY|KITOS_IPC_CLOEXEC))return -K_EINVAL;
            flags=(unsigned int)a[2];ro=(flags&KITOS_IPC_READONLY)!=0;
            rc=ipc_shm_open(n,(size_t)a[1],flags&~KITOS_IPC_CLOEXEC,&h);
        }
        return rc?rc:install(p,h,flags,ro);
    }
    return -K_ENOSYS;
}
