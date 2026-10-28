#include "internal.h"
#include <kernel/task.h>
#include <kernel/error.h>
#include <lib/string.h>
#include <uapi/syscall.h>
#include <uapi/system.h>
#define RETRY_BUSY(expression) do { rc=(expression); if(rc==-K_EBUSY)task_sleep_ms(1); } while(rc==-K_EBUSY)
struct directory_cursor { uint64_t index, current; bool found; struct kitos_dirent entry; };
static int entry(void *context,const struct vfs_dirent *item)
{
    struct directory_cursor *c=context;
    if(c->current++==c->index){
        c->entry.size=item->size;c->entry.kind=item->kind;
        strcpy(c->entry.name,item->name);c->found=true;
    }
    return 0;
}
int64_t process_fs_call(struct process *p,uint64_t number,const uint64_t a[6])
{
    int rc=0;struct vm_space *space=task_current_space();
    if(number==KITOS_SYS_GETCWD){
        size_t n=strlen(p->cwd)+1;if(a[1]<n)return -K_ENAMETOOLONG;
        return copy_to_user(space,a[0],p->cwd,n);
    }
    if(number==KITOS_SYS_FTRUNCATE){
        if(a[0]>=PROCESS_FD_MAX||p->fd[a[0]].kind!=FD_FILE||!(p->fd[a[0]].flags&VFS_WRITE))return -K_EBADF;
        if((int64_t)a[1]<0)return -K_EINVAL;
        RETRY_BUSY(vfs_ftruncate(p->fd[a[0]].file,a[1]));return rc;
    }
    char path[VFS_PATH_MAX];rc=process_user_path(a[0],path,sizeof(path));if(rc)return rc;
    if(number==KITOS_SYS_READDIR){
        if(vm_user_check(space,a[2],sizeof(struct kitos_dirent),VM_WRITE))return -K_EFAULT;
        struct directory_cursor cursor;
        do{cursor=(struct directory_cursor){.index=a[1]};rc=vfs_list(path,entry,&cursor);if(rc==-K_EBUSY)task_sleep_ms(1);}while(rc==-K_EBUSY);
        if(rc)return rc;
        if(!cursor.found)return 0;
        rc=copy_to_user(space,a[2],&cursor.entry,sizeof(cursor.entry));return rc?rc:1;
    }
    if(number==KITOS_SYS_CHDIR){
        RETRY_BUSY(vfs_resolve_directory(path,p->cwd));return rc;
    }
    /* Namespace mutations may stay busy until this same process closes a
     * file. Return EBUSY instead of holding a cancellation pin indefinitely. */
    if(number==KITOS_SYS_MKDIR)return vfs_mkdir(path);
    if(number==KITOS_SYS_RMDIR)return vfs_rmdir(path);
    if(number==KITOS_SYS_RENAME){char other[VFS_PATH_MAX];rc=process_user_path(a[1],other,sizeof(other));return rc?rc:vfs_rename(path,other);}
    return -K_ENOSYS;
}
